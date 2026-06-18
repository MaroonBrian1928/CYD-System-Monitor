const maxDataPoints = 30
const cpuData = Array(maxDataPoints).fill(0)
const memoryData = Array(maxDataPoints).fill(0)
const labels = Array(maxDataPoints).fill('')

const cpuChart = new Chart(document.getElementById('cpuChart').getContext('2d'), {
  type: 'line',
  data: {
    labels: labels,
    datasets: [
      {
        label: 'CPU Usage %',
        data: cpuData,
        borderColor: getComputedStyle(document.documentElement).getPropertyValue('--primary-color'),
        tension: 0.4,
        fill: false
      }
    ]
  },
  options: {
    responsive: true,
    maintainAspectRatio: false,
    scales: {
      y: {
        beginAtZero: true,
        max: 100,
        ticks: {
          color: getComputedStyle(document.documentElement).getPropertyValue('--text-color')
        },
        grid: {
          color: getComputedStyle(document.documentElement).getPropertyValue('--border-color')
        }
      },
      x: {
        display: false
      }
    },
    plugins: {
      legend: {
        display: false
      },
      tooltip: {
        enabled: false
      }
    }
  }
})

const memoryChart = new Chart(document.getElementById('memoryChart').getContext('2d'), {
  type: 'line',
  data: {
    labels: labels,
    datasets: [
      {
        label: 'Memory Usage %',
        data: memoryData,
        borderColor: getComputedStyle(document.documentElement).getPropertyValue('--success-color'),
        tension: 0.4,
        fill: false
      }
    ]
  },
  options: {
    responsive: true,
    maintainAspectRatio: false,
    scales: {
      y: {
        beginAtZero: true,
        max: 100,
        ticks: {
          color: getComputedStyle(document.documentElement).getPropertyValue('--text-color')
        },
        grid: {
          color: getComputedStyle(document.documentElement).getPropertyValue('--border-color')
        }
      },
      x: {
        display: false
      }
    },
    plugins: {
      legend: {
        display: false
      },
      tooltip: {
        enabled: false
      }
    }
  }
})

fetch('/settings')
  .then((response) => response.json())
  .then((data) => {
    const darkMode = data.darkMode
    document.getElementById('darkMode').checked = darkMode
    document.getElementById('themeState').textContent = darkMode ? 'DARK' : 'LIGHT'
    document.documentElement.setAttribute('data-theme', darkMode ? 'dark' : 'light')
    const textColor = getComputedStyle(document.documentElement).getPropertyValue('--text-color')
    cpuChart.options.scales.y.ticks.color = textColor
    memoryChart.options.scales.y.ticks.color = textColor
    cpuChart.update()
    memoryChart.update()
    document.getElementById('beszelHost').value = data.beszel_host
    document.getElementById('beszelPort').value = data.beszel_port
    updateServerDisplay()
    populateTouchCalibration(data)
    populateAutoRotate(data)
    populateBacklightTimeout(data)
    populateDisplayFlip(data)
  })

const tempGauge = new Gauge(document.getElementById('tempGauge')).setOptions({
  angle: 0,
  lineWidth: 0.3,
  radiusScale: 0.9,
  pointer: {
    length: 0.5,
    strokeWidth: 0.035,
    color: getComputedStyle(document.documentElement).getPropertyValue('--danger-color')
  },
  limitMax: true,
  limitMin: true,
  colorStart: getComputedStyle(document.documentElement).getPropertyValue('--danger-color'),
  colorStop: getComputedStyle(document.documentElement).getPropertyValue('--danger-color'),
  strokeColor: getComputedStyle(document.documentElement).getPropertyValue('--border-color'),
  generateGradient: true,
  highDpiSupport: true,
  percentColors: [
    [0.0, '#10b981'],
    [0.5, '#eab308'],
    [1.0, '#ef4444']
  ],
  renderTicks: {
    divisions: 5,
    divWidth: 1.1,
    divLength: 0.7,
    divColor: getComputedStyle(document.documentElement).getPropertyValue('--border-color'),
    subDivisions: 3,
    subLength: 0.5,
    subWidth: 0.6,
    subColor: getComputedStyle(document.documentElement).getPropertyValue('--border-color')
  }
})

tempGauge.maxValue = 100
tempGauge.setMinValue(0)
tempGauge.animationSpeed = 32
tempGauge.set(0)

function updateSignalStrength(dbm) {
  const bars = document.querySelectorAll('.signal-bar')
  const value = document.getElementById('signalValue')
  value.textContent = dbm + ' dBm'

  const quality = Math.max(0, Math.min(100, (dbm + 90) * 2.5))

  const getStrengthClass = (barIndex, quality) => {
    const threshold = (barIndex + 1) * 25
    if (quality >= threshold) {
      if (quality >= 75) return 'excellent'
      if (quality >= 50) return 'good'
      if (quality >= 25) return 'fair'
      return 'poor'
    }
    return ''
  }

  bars.forEach((bar, index) => {
    const threshold = (index + 1) * 25
    const isActive = quality >= threshold
    bar.classList.toggle('active', isActive)
    bar.classList.remove('excellent', 'good', 'fair', 'poor')
    if (isActive) {
      bar.classList.add(getStrengthClass(index, quality))
    }
  })
}

function updateVisualizations(data) {
  const cpuUsage = parseFloat(data.cpuUsage)
  cpuData.push(cpuUsage)
  cpuData.shift()
  cpuChart.update()
  document.querySelector(
    '.chart-card:nth-child(1) h2'
  ).innerHTML = `<i class="ri-cpu-line"></i> CPU Usage <span class="current-value">${cpuUsage.toFixed(1)}%</span>`
  const totalHeap = parseFloat(data.totalHeap)
  const freeHeap = parseFloat(data.freeHeap)
  const usedHeap = totalHeap - freeHeap
  const memoryUsage = Math.max(0, Math.min(100, (usedHeap / totalHeap) * 100))
  memoryData.push(memoryUsage)
  memoryData.shift()
  memoryChart.update()
  document.querySelector(
    '.chart-card:nth-child(2) h2'
  ).innerHTML = `<i class="ri-database-2-line"></i> Memory <span class="current-value">${memoryUsage.toFixed(
    1
  )}%</span>`
  const temp = parseFloat(data.temperature)
  tempGauge.set(temp)
  document.querySelector(
    '.chart-card:nth-child(3) h2'
  ).innerHTML = `<i class="ri-temp-hot-line"></i> Temperature <span class="current-value">${temp.toFixed(1)}°C</span>`
  document.querySelector(
    '.chart-card:nth-child(4) h2'
  ).innerHTML = `<i class="ri-wifi-line"></i> WiFi Signal <span class="current-value">${data.wifiStrength} dBm</span>`
  updateSignalStrength(data.wifiStrength)
}

function updateTheme() {
  const darkMode = document.getElementById('darkMode').checked
  document.documentElement.setAttribute('data-theme', darkMode ? 'dark' : 'light')
  document.getElementById('themeState').textContent = darkMode ? 'DARK' : 'LIGHT'

  const textColor = getComputedStyle(document.documentElement).getPropertyValue('--text-color')
  cpuChart.options.scales.y.ticks.color = textColor
  memoryChart.options.scales.y.ticks.color = textColor
  cpuChart.update()
  memoryChart.update()

  fetch('/settings', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify({ darkMode })
  })
}

function formatUptime(seconds) {
  const days = Math.floor(seconds / 86400)
  const hours = Math.floor((seconds % 86400) / 3600)
  const minutes = Math.floor((seconds % 3600) / 60)
  const secs = seconds % 60

  if (days > 0) return `${days}d ${hours}h ${minutes}m`
  if (hours > 0) return `${hours}h ${minutes}m ${secs}s`
  if (minutes > 0) return `${minutes}m ${secs}s`
  return `${secs}s`
}

function updateThemeColor(property, value) {
  const darkMode = document.getElementById('darkMode').checked
  const color = parseInt(value.substring(1), 16)

  fetch('/settings', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify({
      darkMode: darkMode,
      [property]: color
    })
  }).then(() => {
    setTimeout(updateColorPickers, 100)
  })
}

function updateColorPickers() {
  fetch('/settings')
    .then((response) => response.json())
    .then((data) => {
      const pickers = {
        bg_color: 'bgColor',
        card_bg_color: 'cardBgColor',
        text_color: 'textColor',
        cpu_color: 'cpuColor',
        ram_color: 'ramColor',
        border_color: 'borderColor'
      }

      Object.entries(pickers).forEach(([key, id]) => {
        const picker = document.getElementById(id)
        if (picker) {
          picker.value = data[id]
        }
      })
    })
}

function resetTheme() {
  fetch('/resetTheme', {
    method: 'POST'
  })
    .then((response) => response.json())
    .then((data) => {
      if (data.status === 'success') {
        window.location.reload()
      }
    })
}

function restartESP() {
  if (confirm('Are you sure you want to restart the ESP32?')) {
    fetch('/restart', { method: 'POST' }).then(() => {
      alert('ESP32 is restarting...')
      setTimeout(() => {
        window.location.reload()
      }, 5000)
    })
  }
}

function updateSystemInfo(data) {
  Object.keys(data).forEach((key) => {
    const element = document.getElementById(key)
    if (element) {
      let value = data[key]

      if (key === 'cpuFreqMHz') value += ' MHz'
      if (key === 'flashChipSpeed') value += ' MHz'
      if (key.includes('Size') || key.includes('Heap') || key.includes('Space')) value += ' KB'
      if (key === 'temperature') value += ' °C'
      if (key === 'wifiStrength') value += ' dBm'
      if (key === 'heapFragmentation') value += '%'

      element.textContent = value
    }
  })
}

let isEditing = false

function toggleServerEdit() {
  const displayEl = document.querySelector('.server-display')
  const editEl = document.querySelector('.server-edit')
  editEl.classList.toggle('active')
  displayEl.style.display = editEl.classList.contains('active') ? 'none' : 'flex'
  isEditing = editEl.classList.contains('active')
}

function updateServerDisplay() {
  const host = document.getElementById('beszelHost').value
  const port = document.getElementById('beszelPort').value
  const displayEl = document.getElementById('serverDisplay')

  if (host && port) {
    displayEl.textContent = `${host}:${port}`
  } else {
    displayEl.textContent = 'Not configured'
  }
}

let dataReceived = false
const loader = document.querySelector('.loader-container')

setInterval(() => {
  fetch('/settings')
    .then((response) => response.json())
    .then((data) => {
      if (!dataReceived) {
        dataReceived = true
        loader.classList.add('hidden')
      }
      updateSystemInfo(data)
      updateVisualizations(data)
      updateColorPickers()
      updateTouchRaw(data)

      if (!isEditing) {
        document.getElementById('beszelHost').value = data.beszel_host
        document.getElementById('beszelPort').value = data.beszel_port
        updateServerDisplay()
      }
    })
    .catch((error) => {
      console.error('Error:', error)
      dataReceived = false
      loader.classList.remove('hidden')
    })
}, 2000)

function saveBeszelSettings() {
  const host = document.getElementById('beszelHost').value
  const port = parseInt(document.getElementById('beszelPort').value)

  if (!host || !port) {
    alert('Please enter both host and port')
    return
  }

  fetch('/settings', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify({
      beszel_host: host,
      beszel_port: port
    })
  })
    .then((response) => response.json())
    .then((data) => {
      if (data.status === 'success') {
        updateServerDisplay()
        toggleServerEdit()
        alert('Beszel settings updated successfully!')
      }
    })
    .catch((error) => {
      console.error('Error:', error)
      alert('Failed to update Beszel settings')
    })
}

// --- Touch calibration ---
let touchCalInitialized = false

// Fill the calibration inputs once (and after a reset) so the 2s polling loop
// doesn't overwrite values while you're editing them.
function populateTouchCalibration(data) {
  if (touchCalInitialized || data.touch_x_min === undefined) return
  document.getElementById('touchXMin').value = data.touch_x_min
  document.getElementById('touchXMax').value = data.touch_x_max
  document.getElementById('touchYMin').value = data.touch_y_min
  document.getElementById('touchYMax').value = data.touch_y_max
  document.getElementById('touchSwapXY').checked = data.touch_swap_xy
  document.getElementById('touchInvertX').checked = data.touch_invert_x
  document.getElementById('touchInvertY').checked = data.touch_invert_y
  touchCalInitialized = true
}

function updateTouchRaw(data) {
  if (data.touch_raw_x === undefined) return
  document.getElementById('touchRawLive').textContent =
    data.touch_raw_x + ', ' + data.touch_raw_y
}

function saveTouchCalibration() {
  fetch('/settings', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      touch_x_min: parseInt(document.getElementById('touchXMin').value),
      touch_x_max: parseInt(document.getElementById('touchXMax').value),
      touch_y_min: parseInt(document.getElementById('touchYMin').value),
      touch_y_max: parseInt(document.getElementById('touchYMax').value),
      touch_swap_xy: document.getElementById('touchSwapXY').checked,
      touch_invert_x: document.getElementById('touchInvertX').checked,
      touch_invert_y: document.getElementById('touchInvertY').checked
    })
  })
    .then((response) => response.json())
    .then((data) => {
      if (data.status === 'success') alert('Touch calibration saved!')
    })
    .catch((error) => {
      console.error('Error:', error)
      alert('Failed to save touch calibration')
    })
}

function resetTouchCalibration() {
  fetch('/settings', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ reset_touch: true })
  })
    .then((response) => response.json())
    .then((data) => {
      if (data.status !== 'success') return
      touchCalInitialized = false // re-pull defaults into the inputs
      return fetch('/settings')
        .then((r) => r.json())
        .then((d) => populateTouchCalibration(d))
    })
    .catch((error) => {
      console.error('Error:', error)
      alert('Failed to reset touch calibration')
    })
}

// --- Auto-cycle pages ---
let autoRotateInitialized = false

function populateAutoRotate(data) {
  if (autoRotateInitialized || data.auto_rotate === undefined) return
  document.getElementById('autoRotate').checked = data.auto_rotate
  document.getElementById('autoRotateInterval').value = data.auto_rotate_interval
  autoRotateInitialized = true
}

function saveAutoRotate() {
  let interval = parseInt(document.getElementById('autoRotateInterval').value)
  if (!interval || interval < 1) interval = 1
  fetch('/settings', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      auto_rotate: document.getElementById('autoRotate').checked,
      auto_rotate_interval: interval
    })
  })
    .then((response) => response.json())
    .then((data) => {
      if (data.status === 'success') alert('Auto-cycle settings saved!')
    })
    .catch((error) => {
      console.error('Error:', error)
      alert('Failed to save auto-cycle settings')
    })
}

// --- Screen timeout ---
let backlightInitialized = false

function populateBacklightTimeout(data) {
  if (backlightInitialized || data.backlight_timeout === undefined) return
  document.getElementById('backlightTimeout').value = data.backlight_timeout
  backlightInitialized = true
}

function saveBacklightTimeout() {
  let secs = parseInt(document.getElementById('backlightTimeout').value)
  if (isNaN(secs) || secs < 0) secs = 0
  fetch('/settings', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ backlight_timeout: secs })
  })
    .then((response) => response.json())
    .then((data) => {
      if (data.status === 'success') alert('Screen timeout saved!')
    })
    .catch((error) => {
      console.error('Error:', error)
      alert('Failed to save screen timeout')
    })
}

// --- Display orientation ---
let displayFlipInitialized = false

function populateDisplayFlip(data) {
  if (displayFlipInitialized || data.display_flip === undefined) return
  document.getElementById('displayFlip').checked = data.display_flip
  displayFlipInitialized = true
}

function toggleDisplayFlip() {
  const flipped = document.getElementById('displayFlip').checked
  fetch('/settings', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ display_flip: flipped })
  }).catch((error) => {
    console.error('Error:', error)
    alert('Failed to flip display')
  })
}

let displayOn = true

function toggleDisplay() {
  const screenToggle = document.getElementById('screenToggle')
  displayOn = !screenToggle.checked
  document.getElementById('displayState').textContent = displayOn ? 'ON' : 'OFF'

  fetch('/displaySleep', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify({ sleep: !displayOn })
  })
    .then((response) => response.json())
    .then((data) => {
      if (!data.success) {
        screenToggle.checked = !screenToggle.checked
        displayOn = !displayOn
        document.getElementById('displayState').textContent = displayOn ? 'ON' : 'OFF'
        alert('Failed to toggle display')
      }
    })
    .catch((error) => {
      console.error('Error:', error)
      screenToggle.checked = !screenToggle.checked
      displayOn = !displayOn
      document.getElementById('displayState').textContent = displayOn ? 'ON' : 'OFF'
      alert('Failed to toggle display')
    })
}
