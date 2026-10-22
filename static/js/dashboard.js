const MAX_POINTS = 60;
const ws = new WebSocket(`ws://${location.host}/ws/dashboard`);
const charts = {};

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
    const map = {
        radiatorInlet: 'radiatorInlet',
        radiatorOutlet: 'radiatorOutlet',
        engineInlet: 'engineInlet',
        ambientTemp: 'ambientTemp',
        rpm: 'rpm',
        fanSpeed: 'fanSpeed',
        flowRate: 'flowRate',
        efficiency: 'efficiency',
        coolingRate: 'coolingRate',
        heatDissipation: 'heatDissipation',
        tempRateOfChange: 'tempRateOfChange',
        radiatorTempDiff: 'radiatorTempDiff',
    };
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