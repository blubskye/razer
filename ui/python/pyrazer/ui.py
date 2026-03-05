#   Razer device QT configuration tool
#
#   Copyright (C) 2007-2018 Michael Buesch <m@bues.ch>
#
#   This program is free software; you can redistribute it and/or
#   modify it under the terms of the GNU General Public License
#   as published by the Free Software Foundation; either version 2
#   of the License, or (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.

import copy
import sys
from PyQt6.QtCore import *
from PyQt6.QtGui import *
from PyQt6.QtWidgets import *
Signal = pyqtSignal
from functools import partial
from pyrazer import *

try:
	PYRAZER_SETUP_PY == True
except:
	print("ERROR: Found an old 'pyrazer' module.")
	print("You should uninstall razercfg from the system (see README)")
	print("and re-install it properly.")
	sys.exit(1)

razer = None

# ---------------------------------------------------------------------------
# Async worker helpers
# ---------------------------------------------------------------------------

class _WorkerSignals(QObject):
	finished = pyqtSignal(object)

class _Worker(QRunnable):
	"""Run fn(*args, **kwargs) on a thread-pool thread.
	Emits signals.finished(result) on completion (result may be an Exception).
	The finished signal is delivered to the Qt main thread via the event loop."""
	def __init__(self, fn, *args, **kwargs):
		super().__init__()
		self._fn = fn
		self._args = args
		self._kwargs = kwargs
		self.signals = _WorkerSignals()
		self.setAutoDelete(True)

	def run(self):
		try:
			result = self._fn(*self._args, **self._kwargs)
		except Exception as e:
			result = e
		self.signals.finished.emit(result)

def _run_async(fn, on_done, *args, **kwargs):
	"""Dispatch fn to the global thread pool; call on_done(result) on the main thread."""
	w = _Worker(fn, *args, **kwargs)
	w.signals.finished.connect(on_done)
	QThreadPool.globalInstance().start(w)

# ---------------------------------------------------------------------------

class Wrapper(object):
	def __init__(self, obj):
		self.obj = obj

	def __eq__(self, other):
		return self.obj == other.obj

	def __ne__(self, other):
		return self.obj != other.obj

class WrappedComboBox(QComboBox):
	def addItem(self, text, dataObj=None):
		QComboBox.addItem(self, text, Wrapper(dataObj))

	def itemData(self, index):
		return QComboBox.itemData(self, index).obj

	def findData(self, dataObj):
		for i in range(0, self.count()):
			if self.itemData(i) == dataObj:
				return i
		return -1

# ---------------------------------------------------------------------------
# Button configuration dialog
# ---------------------------------------------------------------------------

class OneButtonConfig(QWidget):
	def __init__(self, id, name, supportedFunctions, buttonConfWidget):
		QWidget.__init__(self, buttonConfWidget)
		self.setContentsMargins(QMargins())
		self.buttonConfWidget = buttonConfWidget

		self.id = id
		self.name = name

		self.setLayout(QHBoxLayout(self))
		self.layout().setContentsMargins(QMargins())

		self.nameLabel = QLabel(name, self)
		self.layout().addWidget(self.nameLabel)
		self.layout().addStretch()
		l = QLabel(self.tr("button is assigned to function"), self)
		self.layout().addWidget(l)

		self.funcCombo = WrappedComboBox(self)
		for func in supportedFunctions:
			self.funcCombo.addItem(func[1], func[0])
		curFunc = razer.getButtonFunction(buttonConfWidget.profileWidget.mouseWidget.mouse,
						  buttonConfWidget.profileWidget.profileId,
						  id)
		self.initialFunc = curFunc[0]
		index = self.funcCombo.findData(curFunc[0])
		if index >= 0:
			self.funcCombo.setCurrentIndex(index)
		self.layout().addWidget(self.funcCombo)

	def getId(self):
		return self.id

	def getSelectedFunction(self):
		index = self.funcCombo.currentIndex()
		return self.funcCombo.itemData(index)

	def getInitialFunction(self):
		return self.initialFunc

class ButtonConfDialog(QDialog):
	def __init__(self, profileWidget):
		QDialog.__init__(self, profileWidget)
		self.profileWidget = profileWidget
		self.setWindowTitle(self.tr("Configure buttons"))

		self.setLayout(QVBoxLayout(self))

		h = QHBoxLayout()
		l = QLabel(self.tr("Physical button"), self)
		h.addWidget(l)
		h.addStretch()
		l = QLabel(self.tr("Assigned function"), self)
		h.addWidget(l)
		self.layout().addLayout(h)

		funcs = razer.getSupportedButtonFunctions(profileWidget.mouseWidget.mouse)
		self.buttons = []
		for b in razer.getSupportedButtons(profileWidget.mouseWidget.mouse):
			button = OneButtonConfig(b[0], b[1], funcs, self)
			self.layout().addWidget(button)
			self.buttons.append(button)

		h = QHBoxLayout()
		self.applyButton = QPushButton(self.tr("Apply"), self)
		self.applyButton.clicked.connect(self._onApply)
		h.addWidget(self.applyButton)
		self.cancelButton = QPushButton(self.tr("Cancel"), self)
		self.cancelButton.clicked.connect(self._onCancel)
		h.addWidget(self.cancelButton)
		self.layout().addLayout(h)

		self._status = QLabel(self)
		self.layout().addWidget(self._status)

	def _onCancel(self):
		self.done(0)

	def _onApply(self):
		changes = [(b.getId(), b.getSelectedFunction())
		           for b in self.buttons
		           if b.getSelectedFunction() != b.getInitialFunction()]
		if not changes:
			self.done(1)
			return

		mouse     = self.profileWidget.mouseWidget.mouse
		profileId = self.profileWidget.profileId

		self.applyButton.setEnabled(False)
		self.cancelButton.setEnabled(False)
		self._status.setText(self.tr("Applying…"))

		def work():
			errors = 0
			for bid, func in changes:
				if razer.setButtonFunction(mouse, profileId, bid, func):
					errors += 1
			return errors

		def on_done(result):
			self.applyButton.setEnabled(True)
			self.cancelButton.setEnabled(True)
			if isinstance(result, Exception):
				self._status.setText(self.tr("Error: %s") % result)
			elif result:
				self._status.setText(self.tr("%d button(s) failed to apply.") % result)
			else:
				self.done(1)

		_run_async(work, on_done)

# ---------------------------------------------------------------------------
# LED widgets
# ---------------------------------------------------------------------------

class OneLedConfig(QWidget):
	def __init__(self, ledsWidget, led):
		QWidget.__init__(self, ledsWidget)
		self.setContentsMargins(QMargins())
		self.ledsWidget = ledsWidget
		self.led = led

		self.setLayout(QGridLayout(self))
		self.layout().setContentsMargins(QMargins())

		self.stateCb = QCheckBox(led.name + " LED", self)
		self.layout().addWidget(self.stateCb, 0, 0)
		self.stateCb.setCheckState(Qt.CheckState.Checked if led.state else Qt.CheckState.Unchecked)

		if led.supported_modes:
			self.modeComBox = WrappedComboBox(self)
			self.layout().addWidget(self.modeComBox, 0, 1)
			for mode in led.supported_modes:
				self.modeComBox.addItem(mode.toString(), mode.val)

			self.modeComBox.currentIndexChanged.connect(self._modeChanged)

			index = self.modeComBox.findData(led.mode.val)
			if index >= 0:
				self.modeComBox.setCurrentIndex(index)
		else:
			self.modeComBox = None

		if led.color is not None and led.canChangeColor:
			self.colorPb = QPushButton(self.tr("change color..."), self)
			self.layout().addWidget(self.colorPb, 0, 2)
			self.colorPb.released.connect(self._colorChangePressed)
		else:
			self.colorPb = None

		# Connect state change last so initial setCheckState doesn't fire it
		self.stateCb.stateChanged.connect(self._toggled)

	# These callbacks only update local state — no razerd calls.
	def _toggled(self, state):
		self.led.state = bool(state)

	def _colorChangePressed(self):
		c = QColor(self.led.color.r, self.led.color.g, self.led.color.b)
		c = QColorDialog.getColor(c, self, self.led.name + self.tr(" color"))
		if not c.isValid():
			return
		self.led.color.r = c.red()
		self.led.color.g = c.green()
		self.led.color.b = c.blue()

	def _modeChanged(self, currentIndex):
		if self.modeComBox:
			self.led.mode.val = self.modeComBox.itemData(currentIndex)

class LedsWidget(QGroupBox):
	def __init__(self, parent, mouseWidget):
		QGroupBox.__init__(self, "LEDs", parent)
		self.mouseWidget = mouseWidget

		self.setLayout(QVBoxLayout(self))
		self.leds = []

		row = QHBoxLayout()
		self._applyBtn = QPushButton(self.tr("Apply"))
		self._applyBtn.clicked.connect(self._onApply)
		row.addWidget(self._applyBtn)
		row.addStretch()
		self._status = QLabel()
		row.addWidget(self._status)
		self.layout().addLayout(row)

	def clear(self):
		for led in self.leds:
			led.deleteLater()
		self.leds = []
		self._status.setText("")
		self.setEnabled(False)
		self.hide()

	def add(self, led):
		oneLed = OneLedConfig(self, led)
		# Insert before the Apply row (last layout item)
		self.layout().insertWidget(self.layout().count() - 1, oneLed)
		self.leds.append(oneLed)
		self.setEnabled(True)
		self.show()

	def updateContent(self, profileId=Razer.PROFILE_INVALID):
		for led in razer.getLeds(self.mouseWidget.mouse, profileId):
			self.add(led)
			self.show()

	def _onApply(self):
		if not self.leds:
			return
		# Snapshot all LED state on the main thread before dispatching
		mouse    = self.mouseWidget.mouse
		led_copies = [copy.deepcopy(lw.led) for lw in self.leds]

		self._applyBtn.setEnabled(False)
		self._status.setText(self.tr("Applying…"))

		def work():
			errors = 0
			for led in led_copies:
				try:
					if razer.setLed(mouse, led):
						errors += 1
				except Exception:
					errors += 1
			return errors

		def on_done(result):
			self._applyBtn.setEnabled(True)
			if isinstance(result, Exception):
				self._status.setText(self.tr("Error: %s") % result)
			elif result:
				self._status.setText(self.tr("%d LED(s) failed.") % result)
			else:
				self._status.setText(self.tr("LEDs applied."))

		_run_async(work, on_done)

# ---------------------------------------------------------------------------
# Frequency widget
# ---------------------------------------------------------------------------

class MouseScanFreqWidget(QWidget):
	def __init__(self, parent, mouseWidget, profileId=Razer.PROFILE_INVALID):
		QWidget.__init__(self, parent)
		self.setContentsMargins(QMargins())
		self.mouseWidget = mouseWidget
		self.profileId = profileId

		self.setLayout(QGridLayout(self))
		self.layout().setContentsMargins(QMargins())

		self.layout().addWidget(QLabel(self.tr("Scan frequency:"), self), 0, 0)
		self.freqSel = WrappedComboBox(self)
		self.layout().addWidget(self.freqSel, 0, 1)

		row = QHBoxLayout()
		self._applyBtn = QPushButton(self.tr("Apply"))
		self._applyBtn.clicked.connect(self._onApply)
		row.addWidget(self._applyBtn)
		row.addStretch()
		self._status = QLabel()
		row.addWidget(self._status)
		self.layout().addLayout(row, 1, 0, 1, 2)

	def updateContent(self):
		self.mouseWidget.recurseProtect += 1

		self.freqSel.clear()
		supportedFreqs = razer.getSupportedFreqs(self.mouseWidget.mouse)
		curFreq = razer.getCurrentFreq(self.mouseWidget.mouse, self.profileId)
		self.freqSel.addItem(self.tr("Unknown Hz"), 0)
		for freq in supportedFreqs:
			self.freqSel.addItem(self.tr("%u Hz" % freq), freq)
		index = self.freqSel.findData(curFreq)
		if index >= 0:
			self.freqSel.setCurrentIndex(index)

		self.mouseWidget.recurseProtect -= 1

	def _onApply(self):
		idx = self.freqSel.currentIndex()
		if idx < 0:
			return
		freq = self.freqSel.itemData(idx)
		if not freq:
			self._status.setText(self.tr("No frequency selected."))
			return

		mouse     = self.mouseWidget.mouse
		profileId = self.profileId

		self._applyBtn.setEnabled(False)
		self._status.setText(self.tr("Applying…"))

		def work():
			return razer.setFrequency(mouse, profileId, freq)

		def on_done(result):
			self._applyBtn.setEnabled(True)
			if isinstance(result, Exception):
				self._status.setText(self.tr("Error: %s") % result)
			elif result:
				self._status.setText(self.tr("Error %d setting frequency.") % result)
			else:
				self._status.setText(self.tr("Frequency applied."))

		_run_async(work, on_done)

# ---------------------------------------------------------------------------
# DPI mapping widgets
# ---------------------------------------------------------------------------

class OneDpiMapping(QWidget):
	def __init__(self, dpiMappingsWidget, dpimapping, supportedRes=None):
		QWidget.__init__(self, dpiMappingsWidget)
		self.setContentsMargins(QMargins())
		self.dpiMappingsWidget = dpiMappingsWidget
		self.dpimapping = dpimapping
		# pending changes: dimIdx -> (mappingId, dimIdx, newResolution)
		self._changes = {}

		self.setLayout(QHBoxLayout(self))
		self.layout().setContentsMargins(QMargins())

		self.layout().addWidget(QLabel(self.tr("Scan resolution %u:" % (dpimapping.id + 1)),
					self))

		# Lazily fetch supportedRes if not pre-supplied
		if supportedRes is None:
			supportedRes = razer.getSupportedRes(self.dpiMappingsWidget.mouseWidget.mouse)

		haveMultipleDims = len([r for r in dpimapping.res if r is not None]) > 1
		dimNames = ( "X", "Y", "Z" )
		changeSlots = ( self._changedDim0, self._changedDim1, self._changedDim2 )

		self._combos = []
		for dimIdx, thisRes in enumerate([r for r in dpimapping.res if r is not None]):
			combo = WrappedComboBox(self)
			self._combos.append(combo)
			name = self.tr("Unknown DPI")
			if haveMultipleDims:
				name = dimNames[dimIdx] + ": " + name
			combo.addItem(name, 0)
			for res in supportedRes:
				resName = self.tr("%u DPI" % res)
				if haveMultipleDims:
					resName = dimNames[dimIdx] + ": " + resName
				combo.addItem(resName, res)
			index = combo.findData(thisRes)
			if index >= 0:
				combo.setCurrentIndex(index)
			combo.currentIndexChanged.connect(changeSlots[dimIdx])
			combo.setEnabled(dpimapping.mutable)
			self.layout().addWidget(combo)
		self.layout().addStretch()

	def _changedDim0(self, index):
		self._changed(index, 0)

	def _changedDim1(self, index):
		self._changed(index, 1)

	def _changedDim2(self, index):
		self._changed(index, 2)

	def _changed(self, index, dimIdx):
		if index <= 0 or dimIdx >= len(self._combos):
			return
		resolution = self._combos[dimIdx].itemData(index)
		self._changes[dimIdx] = (self.dpimapping.id, dimIdx, resolution)

	def getPendingChanges(self):
		"Return list of (mappingId, dimIdx, resolution) tuples for changed dims."
		return list(self._changes.values())

	def clearPendingChanges(self):
		self._changes.clear()

class MouseDpiMappingsWidget(QGroupBox):
	def __init__(self, parent, mouseWidget):
		QGroupBox.__init__(self, parent.tr("Possible scan resolutions"), parent)
		self.mouseWidget = mouseWidget

		self.setLayout(QVBoxLayout(self))
		self.mappings = []
		self.clear()

	def clear(self):
		for mapping in self.mappings:
			mapping.deleteLater()
		self.mappings = []
		if hasattr(self, '_status'):
			self._status.setText("")
		self.setEnabled(False)
		self.hide()

	def add(self, dpimapping, supportedRes=None):
		mapping = OneDpiMapping(self, dpimapping, supportedRes)
		self.mappings.append(mapping)
		# Insert before the Apply row
		self.layout().insertWidget(self.layout().count() - 1, mapping)
		if dpimapping.mutable:
			self.setEnabled(True)
			self.show()

	def _ensureApplyRow(self):
		if not hasattr(self, '_applyBtn'):
			row = QHBoxLayout()
			self._applyBtn = QPushButton(self.tr("Apply"))
			self._applyBtn.clicked.connect(self._onApply)
			row.addWidget(self._applyBtn)
			row.addStretch()
			self._status = QLabel()
			row.addWidget(self._status)
			self.layout().addLayout(row)

	def updateContent(self, profileId=Razer.PROFILE_INVALID):
		self._ensureApplyRow()
		for dpimapping in razer.getSupportedDpiMappings(self.mouseWidget.mouse):
			if (profileId == Razer.PROFILE_INVALID and dpimapping.profileMask == 0) or\
			   (profileId != Razer.PROFILE_INVALID and dpimapping.profileMask & (1 << profileId)):
				self.add(dpimapping)

	def _onApply(self):
		all_changes = []
		for m in self.mappings:
			all_changes.extend(m.getPendingChanges())
		if not all_changes:
			self._status.setText(self.tr("No changes to apply."))
			return

		mouse = self.mouseWidget.mouse
		changes_snapshot = list(all_changes)

		self._applyBtn.setEnabled(False)
		self._status.setText(self.tr("Applying…"))

		def work():
			errors = 0
			for mappingId, dimIdx, res in changes_snapshot:
				try:
					if razer.changeDpiMapping(mouse, mappingId, dimIdx, res):
						errors += 1
				except Exception:
					errors += 1
			return errors

		def on_done(result):
			self._applyBtn.setEnabled(True)
			if isinstance(result, Exception):
				self._status.setText(self.tr("Error: %s") % result)
			elif result:
				self._status.setText(self.tr("%d mapping(s) failed.") % result)
			else:
				for m in self.mappings:
					m.clearPendingChanges()
				self._status.setText(self.tr("Applied."))

		_run_async(work, on_done)

# ---------------------------------------------------------------------------
# Per-profile widget
# ---------------------------------------------------------------------------

class MouseProfileWidget(QWidget):
	def __init__(self, mouseWidget, profileId):
		QWidget.__init__(self, mouseWidget)
		self.mouseWidget = mouseWidget
		self.profileId = profileId

		minfo = razer.getMouseInfo(mouseWidget.mouse)

		self.setLayout(QGridLayout(self))
		yoff = 0

		self.profileActive = QRadioButton(self.tr("Profile active"), self)
		self.profileActive.toggled.connect(self._activeChanged)
		self.layout().addWidget(self.profileActive, yoff, 0)
		yoff += 1

		self.freqSel = None
		if minfo & Razer.MOUSEINFOFLG_PROFILE_FREQ:
			self.freqSel = MouseScanFreqWidget(self, mouseWidget, profileId)
			self.layout().addWidget(self.freqSel, yoff, 0, 1, 2)
			yoff += 1

		self.resSel = []
		axes = self._getIndependentAxes()
		for axis in axes:
			axisName = axis[1] + " " if axis[1] else ""
			self.layout().addWidget(QLabel(self.tr("%sScan resolution:" % axisName), self), yoff, 0)
			resSel = WrappedComboBox(self)
			resSel.currentIndexChanged.connect(self._resChanged)
			self.layout().addWidget(resSel, yoff, 1)
			self.resSel.append(resSel)
			yoff += 1
		self.resIndependent = QCheckBox(self.tr("Independent resolutions"), self)
		self.resIndependent.stateChanged.connect(self._resIndependentChanged)
		self.layout().addWidget(self.resIndependent, yoff, 1)
		yoff += 1
		if len(axes) <= 1:
			self.resIndependent.hide()

		funcs = razer.getSupportedButtonFunctions(self.mouseWidget.mouse)
		if funcs:
			self.buttonConf = QPushButton(self.tr("Configure buttons"), self)
			self.buttonConf.clicked.connect(self._showButtonConf)
			self.layout().addWidget(self.buttonConf, yoff, 0, 1, 2)
			yoff += 1

		if minfo & Razer.MOUSEINFOFLG_PROFNAMEMUTABLE:
			self.buttonName = QPushButton(self.tr("Change profile name"), self)
			self.buttonName.clicked.connect(self._nameChange)
			self.layout().addWidget(self.buttonName, yoff, 0, 1, 2)
			yoff += 1

		self.dpimappings = MouseDpiMappingsWidget(self, mouseWidget)
		self.dpimappings._ensureApplyRow()
		self.layout().addWidget(self.dpimappings, yoff, 0, 1, 2)
		yoff += 1

		self.leds = LedsWidget(self, mouseWidget)
		self.layout().addWidget(self.leds, yoff, 0, 1, 2)
		yoff += 1

	def _getIndependentAxes(self):
		axes = razer.getSupportedAxes(self.mouseWidget.mouse)
		axes = [axis for axis in axes if (axis[2] & Razer.RAZER_AXIS_INDEPENDENT_DPIMAPPING)]
		if not axes:
			axes = [ (0, "", 0) ]
		return axes

	# ------------------------------------------------------------------
	# Async fetch/apply split for reload
	# ------------------------------------------------------------------

	def _fetchData(self, mouse, profileId):
		"""Fetch all profile data from razerd. Safe to call on background thread."""
		d = {}
		try:
			d['minfo'] = razer.getMouseInfo(mouse)
		except Exception:
			d['minfo'] = 0

		if self.freqSel is not None:
			try:
				d['freq'] = {
					'supported': razer.getSupportedFreqs(mouse),
					'current':   razer.getCurrentFreq(mouse, profileId),
				}
			except Exception:
				d['freq'] = None

		try:
			all_axes = razer.getSupportedAxes(mouse)
			axes = [a for a in all_axes if a[2] & Razer.RAZER_AXIS_INDEPENDENT_DPIMAPPING]
			if not axes:
				axes = [(0, "", 0)]
		except Exception:
			axes = [(0, "", 0)]
		d['axes'] = axes

		try:
			all_mappings  = razer.getSupportedDpiMappings(mouse)
			d['supportedMappings'] = [m for m in all_mappings
			                          if m.profileMask == 0 or
			                             m.profileMask & (1 << profileId)]
			d['axisMappings']      = [razer.getDpiMapping(mouse, profileId, a[0]) for a in axes]
			d['dpimappings']       = all_mappings
			d['supportedRes']      = razer.getSupportedRes(mouse)
		except Exception:
			d['supportedMappings'] = []
			d['axisMappings']      = []
			d['dpimappings']       = []
			d['supportedRes']      = []

		try:
			d['activeProf'] = razer.getActiveProfile(mouse)
		except Exception:
			d['activeProf'] = None

		try:
			d['leds'] = razer.getLeds(mouse, profileId)
		except Exception:
			d['leds'] = []

		return d

	def _applyData(self, d):
		"""Update all widgets from pre-fetched data. Must run on main thread."""
		self.mouseWidget.recurseProtect += 1

		# Frequency
		if self.freqSel and d.get('freq') is not None:
			freq_d = d['freq']
			self.freqSel.freqSel.clear()
			self.freqSel.freqSel.addItem(self.tr("Unknown Hz"), 0)
			for f in freq_d.get('supported', []):
				self.freqSel.freqSel.addItem(self.tr("%u Hz" % f), f)
			idx = self.freqSel.freqSel.findData(freq_d.get('current', 0))
			if idx >= 0:
				self.freqSel.freqSel.setCurrentIndex(idx)

		# Resolution selectors
		axes             = d.get('axes', [(0, "", 0)])
		supportedMappings = d.get('supportedMappings', [])
		axisMappings      = d.get('axisMappings', [])

		for resSel in self.resSel:
			resSel.clear()
		for i, resSel in enumerate(self.resSel):
			resSel.addItem(self.tr("Unknown mapping"), 0xFFFFFFFF)
			for mapping in supportedMappings:
				r    = [x for x in mapping.res if x is not None]
				rStr = "/".join(("%u" % x) if x else self.tr("Unknown") for x in r)
				resSel.addItem(
					self.tr("Scan resolution %u   (%s DPI)" % (mapping.id + 1, rStr)),
					mapping.id)
			if i < len(axisMappings):
				idx = resSel.findData(axisMappings[i])
				if idx >= 0:
					resSel.setCurrentIndex(idx)

		independent = (len(axisMappings) > 1 and
		               any(x != axisMappings[0] for x in axisMappings))
		self.resIndependent.setCheckState(
			Qt.CheckState.Checked if independent else Qt.CheckState.Unchecked)

		# Profile active radio
		activeProf = d.get('activeProf')
		if activeProf is not None:
			self.profileActive.setChecked(activeProf == self.profileId)

		# DPI mappings widget — pass pre-fetched supportedRes to avoid sync calls
		self.dpimappings.clear()
		self.dpimappings._ensureApplyRow()
		supportedRes = d.get('supportedRes')
		for mapping in d.get('dpimappings', []):
			if (mapping.profileMask == 0) or (mapping.profileMask & (1 << self.profileId)):
				self.dpimappings.add(mapping, supportedRes)

		# LEDs
		self.leds.clear()
		for led in d.get('leds', []):
			self.leds.add(led)

		self.mouseWidget.recurseProtect -= 1

	def reload(self):
		"""Trigger async reload of this profile widget's data."""
		mouse     = self.mouseWidget.mouse
		profileId = self.profileId

		def fetch():
			return self._fetchData(mouse, profileId)

		def apply(data):
			if isinstance(data, Exception):
				return
			self._applyData(data)

		_run_async(fetch, apply)

	# ------------------------------------------------------------------
	# Signal handlers
	# ------------------------------------------------------------------

	def _activeChanged(self, checked):
		if self.mouseWidget.recurseProtect:
			return
		if not checked:
			self.mouseWidget.recurseProtect += 1
			self.profileActive.setChecked(True)
			self.mouseWidget.recurseProtect -= 1
			return

		mouse     = self.mouseWidget.mouse
		profileId = self.profileId
		self.profileActive.setEnabled(False)

		def work():
			return razer.setActiveProfile(mouse, profileId)

		def on_done(result):
			self.profileActive.setEnabled(True)
			if not isinstance(result, Exception) and result == 0:
				self.mouseWidget.reloadProfiles()

		_run_async(work, on_done)

	def _resChanged(self, unused=None):
		if self.mouseWidget.recurseProtect:
			return
		mouse     = self.mouseWidget.mouse
		profileId = self.profileId

		if self.resIndependent.checkState() == Qt.CheckState.Checked:
			for axisId, resSel in enumerate(self.resSel):
				idx = resSel.currentIndex()
				res = resSel.itemData(idx)
				_run_async(
					lambda r=res, a=axisId: razer.setDpiMapping(mouse, profileId, r, a),
					lambda result: None)
		else:
			idx = self.resSel[0].currentIndex()
			res = self.resSel[0].itemData(idx)
			_run_async(
				lambda r=res: razer.setDpiMapping(mouse, profileId, r),
				lambda result: None)
			self.mouseWidget.recurseProtect += 1
			for resSel in self.resSel[1:]:
				resSel.setCurrentIndex(idx)
			self.mouseWidget.recurseProtect -= 1

	def _resIndependentChanged(self, _=None):
		if self.resIndependent.checkState() == Qt.CheckState.Checked:
			for resSel in self.resSel[1:]:
				resSel.setEnabled(True)
		else:
			for resSel in self.resSel[1:]:
				resSel.setEnabled(False)
		self._resChanged()

	def _showButtonConf(self, checked):
		bconf = ButtonConfDialog(self)
		bconf.exec()

	def _nameChange(self, unused):
		mouse     = self.mouseWidget.mouse
		profileId = self.profileId
		# Fetch current name (blocking, but only on explicit user click — acceptable)
		try:
			name = razer.getProfileName(mouse, profileId)
		except Exception:
			name = ""
		(newName, ok) = QInputDialog.getText(self, self.tr("New profile name"),
						     self.tr("New profile name:"),
						     QLineEdit.EchoMode.Normal,
						     name)
		if not ok or newName == name:
			return

		self.buttonName.setEnabled(False)

		def work():
			return razer.setProfileName(mouse, profileId, newName)

		def on_done(result):
			self.buttonName.setEnabled(True)
			if not isinstance(result, Exception):
				self.mouseWidget.reloadProfiles()

		_run_async(work, on_done)

# ---------------------------------------------------------------------------
# Mouse widget (device-level)
# ---------------------------------------------------------------------------

class MouseWidget(QWidget):
	def __init__(self, parent=None):
		QWidget.__init__(self, parent)
		self.recurseProtect = 0

		self.mainwnd = parent

		self.setLayout(QVBoxLayout(self))

		self.mousesel = WrappedComboBox(self)
		self.mousesel.currentIndexChanged.connect(self.mouseChanged)
		self.layout().addWidget(self.mousesel)
		self.layout().addSpacing(15)

		self.profiletab = QTabWidget(self)
		self.layout().addWidget(self.profiletab)
		self.profileWidgets = []

		self.freqSel = MouseScanFreqWidget(self, self)
		self.layout().addWidget(self.freqSel)

		self.dpimappings = MouseDpiMappingsWidget(self, self)
		self.dpimappings._ensureApplyRow()
		self.layout().addWidget(self.dpimappings)

		self.leds = LedsWidget(self, self)
		self.layout().addWidget(self.leds)

		self.layout().addStretch()
		self.fwVer = QLabel(self)
		self.layout().addWidget(self.fwVer)

	def updateContent(self, mice):
		self.mice = mice
		self.mousesel.clear()
		for mouse in mice:
			id = RazerDevId(mouse)
			self.mousesel.addItem("%s   %s-%s %s" % \
				(id.getDevName(), id.getBusType(),
				 id.getBusPosition(), id.getDevId()))

	def mouseChanged(self, index):
		self.profiletab.clear()
		self.profileWidgets = []
		self.dpimappings.clear()
		self.dpimappings._ensureApplyRow()
		self.leds.clear()
		self.profiletab.setEnabled(index > -1)
		if index == -1:
			self.fwVer.clear()
			return
		self.mouse = self.mice[index]

		minfo = razer.getMouseInfo(self.mouse)

		profileIds = razer.getProfiles(self.mouse)
		activeProfileId = razer.getActiveProfile(self.mouse)
		activeWidget = None
		for profileId in profileIds:
			widget = MouseProfileWidget(self, profileId)
			if profileId == activeProfileId:
				activeWidget = widget
			self.profiletab.addTab(widget, str(profileId + 1))
			self.profileWidgets.append(widget)
		self.reloadProfiles()
		if activeWidget:
			self.profiletab.setCurrentWidget(activeWidget)

		# Update global frequency selection (if any)
		if minfo & Razer.MOUSEINFOFLG_GLOBAL_FREQ:
			self.freqSel.updateContent()
			self.freqSel.show()
		else:
			self.freqSel.hide()

		# Update global DPI mappings (if any)
		self.dpimappings.updateContent()

		# Update global LEDs (if any)
		self.leds.updateContent()

		ver = razer.getFwVer(self.mouse)
		if (ver[0] == 0xFF and ver[1] == 0xFF) or\
		   (ver[0] == 0 and ver[1] == 0):
			self.fwVer.hide()
		else:
			extra = ""
			if minfo & Razer.MOUSEINFOFLG_SUGGESTFWUP:
				extra = self.tr("\nDue to known bugs in this firmware version, a "
					"firmware update is STRONGLY suggested!")
			self.fwVer.setText(self.tr("Firmware version: %u.%02u%s" % (ver[0], ver[1], extra)))
			self.fwVer.show()

	def reloadProfiles(self):
		"""Async reload: fetch all profile data on a background thread, update UI on main thread."""
		mouse       = self.mouse
		prof_list   = [(w, w.profileId) for w in self.profileWidgets]
		tab_list    = [(i, self.profiletab.widget(i).profileId)
		               for i in range(self.profiletab.count())]

		def fetch():
			result = {'active': None, 'names': {}, 'per_profile': {}}
			try:
				result['active'] = razer.getActiveProfile(mouse)
			except Exception:
				pass
			for _i, pid in tab_list:
				try:
					result['names'][pid] = razer.getProfileName(mouse, pid)
				except Exception:
					result['names'][pid] = str(pid + 1)
			for widget, pid in prof_list:
				try:
					result['per_profile'][pid] = widget._fetchData(mouse, pid)
				except Exception:
					result['per_profile'][pid] = None
			return result

		def apply(result):
			if isinstance(result, Exception):
				return
			self.recurseProtect += 1
			active = result.get('active')
			for i, pid in tab_list:
				name = result['names'].get(pid, str(pid + 1))
				if active == pid:
					name = ">" + name + "<"
				self.profiletab.setTabText(i, name)
			for widget, pid in prof_list:
				data = result['per_profile'].get(pid)
				if data is not None:
					widget._applyData(data)
			self.recurseProtect -= 1

		_run_async(fetch, apply)

# ---------------------------------------------------------------------------
# Main window
# ---------------------------------------------------------------------------

class StatusBar(QStatusBar):
	def showMessage(self, msg):
		QStatusBar.showMessage(self, msg, 10000)

class MainWindow(QMainWindow):
	shown = Signal(QWidget)
	hidden = Signal(QWidget)

	def __init__(self, parent = None, enableNotificationPolling = True):
		QMainWindow.__init__(self, parent)
		self.setWindowTitle(self.tr("Razer device configuration"))

		mb = QMenuBar(self)
		rzrmen = QMenu(self.tr("Razer"), mb)
		rzrmen.addAction(self.tr("Scan system for devices"), self.scan)
		rzrmen.addAction(self.tr("Re-apply all hardware settings"), self.reconfig)
		rzrmen.addSeparator()
		rzrmen.addAction(self.tr("Exit"), self.close)
		mb.addMenu(rzrmen)
		helpmen = QMenu(self.tr("Help"), mb)
		helpmen.addAction(self.tr("About"), self.about)
		mb.addMenu(helpmen)
		self.setMenuBar(mb)

		tab = QTabWidget(self)
		self.mousewidget = MouseWidget(self)
		tab.addTab(self.mousewidget, self.tr("Mice"))
		self.setCentralWidget(tab)

		self.setStatusBar(StatusBar())

		self.mice = []
		self.scan()
		if enableNotificationPolling:
			self.__notifyPollTimer = QTimer(self)
			self.__notifyPollTimer.timeout.connect(self.pollNotifications)
			self.__notifyPollTimer.start(300)

	def pollNotifications(self):
		n = razer.pollNotifications()
		if n:
			self.scan()

	def scan(self):
		razer.rescanMice()
		mice = razer.getMice()
		if len(mice) != len(self.mice):
			if (len(mice) == 1):
				self.statusBar().showMessage(self.tr("Found one Razer mouse"))
			elif (len(mice) > 1):
				self.statusBar().showMessage(self.tr("Found %d Razer mice" % len(mice)))
		self.mice = mice
		self.mousewidget.updateContent(mice)

	def reconfig(self):
		razer.rescanDevices()
		razer.reconfigureDevices()

	def about(self):
		QMessageBox.information(self, self.tr("About"),
					self.tr("Razer device configuration tool\n"
						"Version %s\n"
						"Copyright (c) 2007-2026 Michael Buesch et al."
						% RAZER_VERSION))

	def showEvent(self, ev):
		super(MainWindow, self).showEvent(ev)
		self.shown.emit(self)

	def hideEvent(self, ev):
		super(MainWindow, self).hideEvent(ev)
		self.hidden.emit(self)

class AppletMainWindow(MainWindow):
	def __init__(self, parent=None):
		super().__init__(parent, enableNotificationPolling = False)

	def updateContent(self):
		self.mousewidget.reloadProfiles()

	def closeEvent(self, event):
		event.ignore()
		self.hide()

class RazerApplet(QSystemTrayIcon):
	def __init__(self):
		QSystemTrayIcon.__init__(self)

		self.__contextMenuIsShown = False
		self.__mainwndIsShown = False

		icon = QIcon.fromTheme("razercfg")
		if icon.isNull():
			for prefix in ("/usr/local", "/usr"):
				icon.addFile(prefix + "/share/icons/hicolor/" +\
					     "scalable/apps/razercfg.svg")
			icon.addFile("razercfg.svg")
		self.setIcon(icon)

		self.menu = QMenu()
		self.mainwnd = AppletMainWindow()

		self.__pollTimer = QTimer(self)
		self.__pollTimer.setInterval(300)
		self.__pollTimer.stop()

		self.mainwnd.scan()
		self.mice = razer.getMice();
		self.buildMenu()

		self.setContextMenu(self.menu)

		self.__pollTimer.timeout.connect(self.updateContent)
		self.contextMenu().aboutToShow.connect(self.__contextAboutToShow)
		self.contextMenu().aboutToHide.connect(self.__contextAboutToHide)
		self.activated.connect(self.__handleActivate)
		self.mainwnd.shown.connect(self.__mainwndShown)
		self.mainwnd.hidden.connect(self.__mainwndHidden)

	def __handleActivate(self, reason):
		if reason in {QSystemTrayIcon.ActivationReason.Trigger,
			      QSystemTrayIcon.ActivationReason.DoubleClick,
			      QSystemTrayIcon.ActivationReason.MiddleClick}:
			self.contextMenu().popup(QCursor.pos())

	def __contextAboutToShow(self):
		self.__contextMenuIsShown = True
		self.__setPolling()
		self.updateContent()

	def __contextAboutToHide(self):
		self.__contextMenuIsShown = False
		self.__setPolling()

	def __mainwndShown(self, widget):
		self.__mainwndIsShown = True
		self.__setPolling()
		self.updateContent()

	def __mainwndHidden(self, widget):
		self.__mainwndIsShown = False
		self.__setPolling()

	def __setPolling(self):
		if self.__contextMenuIsShown or self.__mainwndIsShown:
			if not self.__pollTimer.isActive():
				self.__pollTimer.start()
		else:
			if self.__pollTimer.isActive():
				self.__pollTimer.stop()

	def updateContent(self):
		if razer.pollNotifications():
			self.mainwnd.scan()
			mice = razer.getMice()
			if mice != self.mice:
				self.mice = mice
				self.buildMenu()

	def buildMenu(self):
		self.menu.clear()

		for mouse in self.mice:
			mouse_id = RazerDevId(mouse)
			mouse_menu = self.menu.addMenu("&" + mouse_id.getDevName() + " mouse")
			self.getMouseProfiles(mouse, mouse_menu)

		if not self.mice:
			act = self.menu.addAction("No Razer devices found")
			act.setEnabled(False)

		self.menu.addSeparator()
		self.menu.addAction("&Open main window...", self.mainwnd.show)
		self.menu.addAction("&Exit", sys.exit)

	def selectProfile(self, mouse, profileId):
		razer.setActiveProfile(mouse, profileId)
		self.mainwnd.updateContent()

	def getMouseProfiles(self, mouse, mouse_menu):
		mouse_menu.clear()
		profileIds = razer.getProfiles(mouse)
		activeProfileId = razer.getActiveProfile(mouse)
		group = QActionGroup(self)
		for profileId in profileIds:
			name = razer.getProfileName(mouse, profileId)
			action = QAction(name, mouse_menu)
			action.setCheckable(True)
			action.triggered.connect( partial(self.selectProfile, mouse, profileId))
			group.addAction(action)
			mouse_menu.addAction(action)
			if profileId == activeProfileId:
				action.setChecked(True)
