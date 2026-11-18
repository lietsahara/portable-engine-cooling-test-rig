const MAX_POINTS = 60;
const wsProtocol = location.protocol === 'https:' ? 'wss://' : 'ws://';
const ws = new WebSocket(`${wsProtocol}${location.host}/ws/dashboard`);const charts = {};

function initChart(id, yLabel, datasets) {
    const ctx = document.getElementById(id).getContext('2d');
    charts[id] = new Chart(ctx, {
        type: 'line',
        data: {
            labels: [],
            datasets: datasets.map(ds => ({
                label: ds.label,
                data: [],
                borderWidth: 2,
                pointRadius: 1,
                tension: 0.2,
                borderColor: ds.color,
            })),
        },
        options: {
            responsive: true,
            animation: false,
            scales: {
                x: { title: { display: true, text: 'Time / Reading' } },
                y: { title: { display: true, text: yLabel } },
            },
            plugins: { legend: { display: true } },
        },
    });
}

initChart('tempVsTime', '°C', [
    { label: 'Radiator Inlet', color: 'red' },
    { label: 'Radiator Outlet', color: 'blue' },
    { label: 'Engine Inlet', color: 'green' },
    { label: 'Ambient', color: 'orange' },
]);
initChart('tempVsRpm', '°C', [
    { label: 'Radiator Inlet', color: 'red' },
    { label: 'Radiator Outlet', color: 'blue' },
    { label: 'Engine Inlet', color: 'green' },
]);
initChart('qVsTime', 'W', [{ label: 'Q', color: 'purple' }]);
initChart('qVsRpm', 'W', [{ label: 'Q', color: 'purple' }]);
initChart('dTdtVsTime', '°C/s', [{ label: 'dT/dt', color: 'brown' }]);
initChart('radiatorDeltaTVsTime', '°C', [{ label: 'ΔT', color: 'teal' }]);
initChart('deltaTVsFlow', '°C', [{ label: 'ΔT', color: 'teal' }]);
initChart('flowVsRpm', 'L/min', [{ label: 'Flow Rate', color: 'cyan' }]);

ws.onmessage = (event) => {
    const data = JSON.parse(event.data);
    updateLiveValues(data);
    addDataPoint(data);
};

function updateLiveValues(d) {
    // The server broadcasts snake_case keys (e.g., radiator_inlet)
    document.getElementById('radiatorInlet').textContent = d.radiator_inlet != null ? d.radiator_inlet.toFixed(1) : '--';
    document.getElementById('radiatorOutlet').textContent = d.radiator_outlet != null ? d.radiator_outlet.toFixed(1) : '--';
    document.getElementById('engineInlet').textContent = d.engine_inlet != null ? d.engine_inlet.toFixed(1) : '--';
    document.getElementById('ambientTemp').textContent = d.ambient_temp != null ? d.ambient_temp.toFixed(1) : '--';
    document.getElementById('rpm').textContent = d.rpm != null ? d.rpm.toFixed(0) : '--';
    document.getElementById('fanSpeed').textContent = d.fan_speed != null ? d.fan_speed.toFixed(0) : '--';
    document.getElementById('flowRate').textContent = d.flow_rate != null ? d.flow_rate.toFixed(1) : '--';
    document.getElementById('coolingRate').textContent = d.cooling_rate != null ? d.cooling_rate.toFixed(1) : '--';
    document.getElementById('heatDissipation').textContent = d.heat_dissipation != null ? d.heat_dissipation.toFixed(0) : '--';
    document.getElementById('tempRateOfChange').textContent = d.temp_rate_of_change != null ? d.temp_rate_of_change.toFixed(2) : '--';
    document.getElementById('radiatorTempDiff').textContent = d.radiator_temp_diff != null ? d.radiator_temp_diff.toFixed(1) : '--';
}
    for (const [key, id] of Object.entries(map)) {
        const el = document.getElementById(id);
        if (el) el.textContent = d[key] != null ? d[key].toFixed(2) : '--';
    }
}

function addDataPoint(d) {
    const label = new Date(d.timestamp).toLocaleTimeString();

    addPoint('tempVsTime', label, [d.radiator_inlet, d.radiator_outlet, d.engine_inlet, d.ambient_temp]);
    if (d.rpm != null) addPointXY('tempVsRpm', d.rpm, [d.radiator_inlet, d.radiator_outlet, d.engine_inlet]);
    if (d.heat_dissipation != null) addPoint('qVsTime', label, [d.heat_dissipation]);
    if (d.heat_dissipation != null && d.rpm != null) addPointXY('qVsRpm', d.rpm, [d.heat_dissipation]);
    if (d.temp_rate_of_change != null) addPoint('dTdtVsTime', label, [d.temp_rate_of_change]);
    if (d.radiator_temp_diff != null) addPoint('radiatorDeltaTVsTime', label, [d.radiator_temp_diff]);
    if (d.radiator_temp_diff != null && d.flow_rate != null) addPointXY('deltaTVsFlow', d.flow_rate, [d.radiator_temp_diff]);
    if (d.flow_rate != null && d.rpm != null) addPointXY('flowVsRpm', d.rpm, [d.flow_rate]);
}

function addPoint(chartId, label, values) {
    const chart = charts[chartId];
    chart.data.labels.push(label);
    values.forEach((v, i) => chart.data.datasets[i].data.push(v));
    trimChart(chart);
    chart.update();
}

function addPointXY(chartId, x, values) {
    const chart = charts[chartId];
    chart.data.labels.push(x);
    values.forEach((v, i) => chart.data.datasets[i].data.push(v));
    trimChart(chart);
    chart.update();
}

function trimChart(chart) {
    if (chart.data.labels.length > MAX_POINTS) {
        chart.data.labels.shift();
        chart.data.datasets.forEach(ds => ds.data.shift());
    }
}