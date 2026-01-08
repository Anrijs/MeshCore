static const char *htmlSettings PROGMEM = R"(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>MeshCore Logger</title>
    <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/milligram@1.4.1/dist/milligram.min.css">
</head>
<body>
    <main id="contents" class="container">
        <section style="margin-bottom: 2.4rem;">
            <h1>MeshCore Logger</h1>
            <p>Node preferences, network configuration, and telemetry control.</p>
        </section>

        <section style="margin-bottom: 2.4rem;">
            <h2>Node Preferences</h2>
            <div id="nodePrefs" class="prefs-group"></div>
        </section>

        <hr>
        <section style="margin-bottom: 2.4rem;">
            <h2>WiFi Preferences</h2>
            <div id="wifiPrefs" class="prefs-group"></div>
        </section>

        <hr>
        <section style="margin-bottom: 2.4rem;">
            <h2>Logger Preferences</h2>
            <div id="loggerPrefs" class="prefs-group"></div>
        </section>

        <hr>
        <section style="margin-bottom: 2.4rem;">
            <div class="row">
                <div class="column">
                    <h2>Telemetry Rules</h2>
                    <p>Define collection paths, schedules, and credentials.</p>
                </div>
                <div class="column" style="text-align: right;">
                    <button id="btnTel" onclick='fetchTelemetry(this)'>Load</button>
                </div>
            </div>
            <div class="prefs-group">
                <table>
                <thead>
                    <tr>
                        <th>ID</th>
                        <th>Name</th>
                        <th>Path</th>
                        <th>Start</th>
                        <th>Interval</th>
                        <th>Password</th>
                        <th></th>
                    </tr>
                </thead>
                <tbody id="telemetryTable">
                </tbody>
            </table>
            <label>Public key</label>
            <div class="row input-group">
                <div class="column">
                    <div style="display: flex; gap: 0.6rem; align-items: center;">
                        <input id="telpk" type="text" placeholder="Add telemetry rule key" oninput='validateAddTelemetry(this)' data-invalid='1' data-changed='0'>
                        <button onclick='addTelemetry()'>Add</button>
                        <button class="button-clear" onclick='clearTelemetryLog()'>Clear</button>
                    </div>
                </div>
            </div>
            <strong>Telemetry log</strong>
            <pre id="tellg"></pre>
            </div>
        </section>

        <hr>
        <section>
            <div class="row">
                <div class="column">
                    <h2>Contacts</h2>
                    <p>Known peers and advertised keys.</p>
                </div>
                <div class="column" style="text-align: right;">
                    <button id="btnCon" onclick='fetchContacts(this)'>Load</button>
                </div>
            </div>
            <div class="prefs-group">
                <div style="overflow-x:auto">
                <table>
                <thead>
                    <tr>
                        <th>ID</th>
                        <th>Name</th>
                        <th>Key</th>
                        <th>Advert</th>
                        <th></th>
                    </tr>
                </thead>
                <tbody id="contactsTable">

                </tbody>
            </table>
                </div>
            </div>
        </section>
    </main>
</body>

<script>
let contacts = [];
let telemetry = [];
let ws;
let reconnectTimeout = null;

function checkWebSocket() {
    if (!ws || ws.readyState !== WebSocket.OPEN) {
        scheduleReconnect();
        return;
    }

    try {
        ws.send(JSON.stringify({ type: "ping" }));
    } catch {
        scheduleReconnect();
    }
}

function connectWs() {
    ws = new WebSocket(`ws://${window.location.host}/ws`);
    ws.onopen = function() {
        console.log("WebSocket connected");
    };
    ws.onmessage = function(event) {
        console.log("WebSocket message: " + event.data);
        const data = JSON.parse(event.data);
        if (data.type === "telemetry_data") {
            let p = document.getElementById('tellg');
            p.innerText += data.data.n + ":\n" + data.data.m + "\n\n";
        }
    };
    ws.onclose = function() {
        console.log("WebSocket closed");
        scheduleReconnect();
    };
    ws.onerror = function(error) {
        console.log(error);
        scheduleReconnect();
    };
}

function scheduleReconnect() {
    if (reconnectTimeout) return;

    reconnectTimeout = setTimeout(() => {
        console.log("Reconnecting WebSocket...");
        reconnectTimeout = null;
        connectWs();
    }, 3000);
}

document.addEventListener("visibilitychange", () => {
    if (document.visibilityState === "visible") {
        checkWebSocket();
    }
});

function removeContact(id) {
    meshExec([`contacts rm ${id}`], rep => {
        contacts.splice(id, 1);
        for (let c of contacts) {
            if (c && c.id > id) {
                c.id--;
            }
        }
        loadContacts({contacts});
    });
}

function removeTelemetry(id) {
    meshExec([`tel rm ${id}`], rep => {
        telemetry.splice(id, 1);
        for (let t of telemetry) {
            if (t && t.id > id) {
                t.id--;
            }
        }
        loadTelemetry({telemetry});
    });
}

function runTelemetry(id) {
    meshExec([`tel schedule ${id}`]);
}

function fetchSettings() {
    const url = "/settings.json";
    fetch(url)
        .then((response) => response.json())
        .then((json) => loadSettings(json));
}

function fetchContacts(e) {
    e.disabled = true;
    document.getElementById("contactsTable").innerHTML = "";
    const url = "/contacts.json";
    fetch(url)
        .then((response) => response.json())
        .then((json) => {
            loadContacts(json);
            e.disabled = false;
        });
}

function fetchTelemetry(e) {
    e.disabled = true;
    document.getElementById("telemetryTable").innerHTML = "";
    const url = "/telemetry.json";
    fetch(url)
        .then((response) => response.json())
        .then((json) => {
            loadTelemetry(json);
            e.disabled = false;
        });
}

function meshExec(cmds, cb=null) {
    fetch("/exec", {
        method: "POST",
        body: JSON.stringify({
            commands: cmds,
        }),
        headers: {
            "Content-type": "application/json; charset=UTF-8"
        }
    })
    .then((response) => response.json())
    .then((json) => {
        if (cb) cb(json);
    });
}

function validateAddTelemetry(e) {
    // oninputs
    e.dataset.changed = 1;
    e.dataset.invalid = 0;
    e.setAttribute("aria-invalid", "false");
    
    if (!/^(?:[0-9a-fA-F]{2})(?:(?::)?[0-9a-fA-F]{2}){1,}$/.test(e.value)) {
        e.dataset.changed = 0;
        e.dataset.invalid = 1;
        e.setAttribute("aria-invalid", "true");
    }
}

function addTelemetry() {
    let k = document.getElementById('telpk');
    let v = k.dataset.invalid === "1";
    if (v) return alert('invalid public key');
    meshExec([`tel add ${k.value.replaceAll(':', '')}`], () => {
        fetchTelemetry(document.getElementById('btnTel'));
    });
    k.value = '';
}

function clearTelemetryLog() {
    let p = document.getElementById('tellg');
    p.innerText = '';
}

function createInput(div, name, type, value, cli, extras={}) {
    let group = document.createElement("div");
    let label = document.createElement("label");
    let title = document.createElement("span");
    let input = document.createElement("input");
    label.appendChild(title);
    label.appendChild(input);
    group.appendChild(label);
    div.appendChild(group)

    group.classList.add("input-group");

    title.innerText = name;
    input.value = value;
    input.type = type;

    if (extras.max || false) {
        input.max = extras.max;
    }

    if (extras.maxlength || false) {
        input.maxlength = extras.maxlength;
    }

    if (extras.min || false) {
        input.min = extras.min;
    }

    return {
        dom: {
            group,
            label,
            title,
            input
        },
        cmd: () => {
            return cli.replace("{}", input.value);
        }
    };
}

function createSaveButton(div, title, inputs) {
    let button = document.createElement("button");
    button.innerText = title;
    button.onclick = (e) => {
        let commands = [];
        for (const input of inputs) {
            commands.push(input.cmd());
        }
        meshExec(commands);
    };

    div.appendChild(button);
    
    return {
        dom: {
            button
        }
    }
}

function loadContacts(data) {
    contacts = data["contacts"];
    const contactsTable = document.getElementById("contactsTable");
    contactsTable.innerHTML = "";

    for (const c of contacts) {
        let row = contactsTable.insertRow();
        let colId = row.insertCell();
        let colNa = row.insertCell();
        let colPk = row.insertCell();
        let colTi = row.insertCell();
        let colAc = row.insertCell();

        colPk.classList.add('pk');

        let btnRm = document.createElement("button");
        btnRm.innerText = "Remove";
        btnRm.onclick = (e) => { removeContact(c.id) };
  
        colId.innerText = c.id;
        colPk.innerText = c.pk;
        colNa.innerText = c.n;
        colTi.innerText = new Date(c.a * 1000).toISOString();

        colAc.append(btnRm);
    }
}

function loadTelemetry(data) {
    telemetry = data["telemetry"];
    const telemetryTable = document.getElementById("telemetryTable");
    telemetryTable.innerHTML = "";

    let createInput = (parent, value, type, validate=undefined) => {
        let input = document.createElement("input");
        input.type = type;
        input.value = value;
        input.changed = false;
        input.oninput = (e) => {
            input.dataset.changed = true;
            input.dataset.invalid = 0;
            input.setAttribute("aria-invalid", "false");
            if (validate) {
                if (!validate(e.target.value)) {
                    input.changed = false;
                    input.dataset.invalid = 1;
                    input.setAttribute("aria-invalid", "true");
                }
            }
        }
        parent.append(input);
        return input;
    }


    for (const t of telemetry) {
        let row = telemetryTable.insertRow();
        let colId = row.insertCell();
        let colNa = row.insertCell();
        let colPa = row.insertCell();
        let colSt = row.insertCell();
        let colIn = row.insertCell();
        let colPw = row.insertCell();
        let colAc = row.insertCell();

        colSt.classList.add('input-group');
        colIn.classList.add('input-group');
        colPw.classList.add('input-group');
        colSt.style.verticalAlign = "middle";
        colIn.style.verticalAlign = "middle";
        colPw.style.verticalAlign = "middle";
        colAc.style.verticalAlign = "middle";
        colAc.style.whiteSpace = "nowrap";

        let actionGroup = document.createElement("div");
        actionGroup.style.display = "flex";
        actionGroup.style.gap = "0.6rem";
        actionGroup.style.alignItems = "center";

        colId.innerText = t.id;
        colNa.innerText = t.name;

        let inPa = createInput(colPa, t.path, "text", (str) => {
            if (str.toLowerCase() == "flood") return true;
            return /^$|^[0-9a-fA-F]{2}(?:,[0-9a-fA-F]{2})*$/.test(str);
        });

        let inSt = createInput(colSt, t.start, "number");
        inSt.min = 0;
        inSt.max = 86400;
        inSt.size = 6;

        let inIn = createInput(colIn, t.interval, "number");
        inIn.min = 0;
        inIn.max = 0;
        inIn.max = 86400;
        inIn.size = 6;

        let inPw = createInput(colPw, t.password, "text");
        inPw.maxlength = 15;

        let btnSv = document.createElement("button");
        btnSv.innerText = "Save";
        btnSv.onclick = (e) => { 
            let cmds = [];
            if (inPa.dataset.changed === "1") {
                cmds.push(`tel set path ${t.id} ${inPa.value}`);
                inPa.dataset.changed = 0;
            }
            if (inSt.dataset.changed === "1") {
                cmds.push(`tel set start ${t.id} ${inSt.value}`);
                inSt.dataset.changed = 0;
            }
            if (inIn.dataset.changed === "1") {
                cmds.push(`tel set interval ${t.id} ${inIn.value}`);
                inIn.dataset.changed = 0;
            }
            if (inPw.dataset.changed === "1") {
                cmds.push(`tel set password ${t.id} ${inPw.value}`);
                inPw.dataset.changed = 0;
            }
            if (cmds.length) meshExec(cmds);
        };
        actionGroup.append(btnSv);
  
        let btnRun = document.createElement("button");
        btnRun.innerText = "Schedule";
        btnRun.onclick = (e) => { runTelemetry(t.id) };  
        actionGroup.append(btnRun);

        let btnRm = document.createElement("button");
        btnRm.innerText = "Remove";
        btnRm.onclick = (e) => { removeTelemetry(t.id) };  
        actionGroup.append(btnRm);
        colAc.append(actionGroup);
    }
}

function loadSettings(data) {
    const nodePrefs = data["node_prefs"];
    const wifiPrefs = data["wifi_prefs"];
    const loggerPrefs = data["logger_prefs"];

    const nodeDiv = document.getElementById("nodePrefs");
    const wifiDiv = document.getElementById("wifiPrefs");
    const loggerDiv = document.getElementById("loggerPrefs");

    nodeDiv.innerHTML = '';
    wifiDiv.innerHTML = '';
    loggerDiv.innerHTML = '';

    let nodePrefsInputs = [];
    let wifiPrefsInputs = [];
    let loggerPrefsInputs = [];


    // Add inputs
    nodePrefsInputs.push(createInput(nodeDiv, "Node Name", "text", nodePrefs.node_name, "set name {}", {maxlength: 31}));
    nodePrefsInputs.push(createInput(nodeDiv, "Latitude", "number", nodePrefs.node_lat, "set lat {}", {min: -90, max: 90}));
    nodePrefsInputs.push(createInput(nodeDiv, "Longitude", "number", nodePrefs.node_lon, "set lon {}", {min: -180, max: 180}));
    nodePrefsInputs.push(createInput(nodeDiv, "Frequency", "number", nodePrefs.freq, "set freq {}", {min: 0}));
    nodePrefsInputs.push(createInput(nodeDiv, "Tx Power dBm", "number", nodePrefs.tx_power_dbm, "set tx {}", {min: 0, max: 30}));
    createSaveButton(nodeDiv, "Save Node Prefs", nodePrefsInputs);

    wifiPrefsInputs.push(createInput(wifiDiv, "SSID", "text", wifiPrefs.ssid, "wifi ssid {}", {maxlength: 32}));
    wifiPrefsInputs.push(createInput(wifiDiv, "Password", "password", wifiPrefs.password, "wifi password {}", {maxlength: 63}));
    wifiPrefsInputs.push(createInput(wifiDiv, "Tx Power dBm", "number", wifiPrefs.txpower, "wifi tx {}", {min: 2, max: 21}));
    createSaveButton(wifiDiv, "Save WiFi Prefs", wifiPrefsInputs);

    loggerPrefsInputs.push(createInput(loggerDiv, "URL", "text", loggerPrefs.url, "log url {}", {maxlength: 255}));
    loggerPrefsInputs.push(createInput(loggerDiv, "Auth", "text", loggerPrefs.auth, "log auth {}", {maxlength: 31}));
    loggerPrefsInputs.push(createInput(loggerDiv, "Self-report", "number", loggerPrefs.selfreport, "log report {}", {max: 0xFFFFFFFF}));
    createSaveButton(loggerDiv, "Save Logger Prefs", loggerPrefsInputs);
}

window.onload = function(e){ 
    connectWs();
    fetchSettings();
}

</script>
</html>
)";

static const char *htmlChat PROGMEM = R"(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>MeshCore Logger</title>
    <style>
        html, body {
            height: 100%;
            margin: 0;
            font-family: sans-serif;
        }

        #container {
            display: flex;
            flex-direction: column;
            height: 100%;
        }

        #messages {
            flex: 1;
            overflow-y: auto;
            padding: 24px;
        }

        #bottom {
            display: flex;
            gap: 12px;
            padding: 10px;
            border-top: 1px solid #ddd;
            background: #fff;
        }

        #chat, #btnsend {
            background: #f0f0f0ff;
            border: solid 1px #999;
            border-radius: 8px;
            padding: 8px;
            font-size: 1.2rem;
        }

        #chat {
            flex: 1;
        }

        #btnsend {
            padding: 0 24px;
        }

        .msg {
            margin: 12px 0
        }

        .msg .user {
            font-weight: bold;
            color: #3db1ff;
            font-size: 0.9rem;
        }

        .msg .text {
            color: #111;
            font-size: 1rem;
        }

        .msg.system .text {
            color: #999;
            font-size: 1rem;
            font-style: italic;
        }

        .msg .date {
            color: #999;
            font-size: 0.8rem;
            margin-left: 8px;
        }
    </style>
</head>
<body>
    <div id="container">
        <div id="messages">
        </div>
        <div id="bottom">
            <input id="chat" type="text">
            <button id="btnsend">&#10148;</button>
        </div>
    </div>
</body>

<script>

function str2color(str, saturation = 65, lightness = 45) {
    let hash = 0x811c9dc5n;
    for (let i = 0; i < str.length; i++) {
        hash = BigInt.asIntN(32, hash ^ BigInt(str.charCodeAt(i)));
        hash = BigInt.asIntN(32, hash * 0x01000193n);
    }

    return `hsl(${Number(hash & 0xFFFFFFFFn) % 360}deg, ${saturation}%, ${lightness}%)`;
}

function fetchChat() {
    const url = "/chat.json";
    fetch(url)
        .then((response) => response.json())
        .then((json) => loadChat(json));
}

function addMessage(msg) {
    if (msg.id != -1 && messages.hasOwnProperty(msg.id)) return;
    messages[msg.id] = msg;

    let msgdiv = document.getElementById("messages");

    let root = document.createElement("div");
    let info = document.createElement("div");
    let user = document.createElement("span");
    let date = document.createElement("span");
    let text = document.createElement("div");

    root.classList.add("msg");
    info.classList.add("info");
    user.classList.add("user");
    date.classList.add("date");
    text.classList.add("text");

    if (msg.classList) {
        root.classList.add(msg.classList);
    }

    root.appendChild(info);
    root.appendChild(text);
    info.appendChild(user);
    info.appendChild(date);

    let idx = msg.m.indexOf(': ');
    let d = new Date(msg.t*1000);

    const dateDate = d.toISOString().split('T')[0];
    const todayISO = new Date().toISOString().split('T')[0];
    const dateTime = d.toLocaleTimeString('en-GB', {
        hour12: false
    });

    if (idx >= 0) {
        user.innerText = msg.m.substr(0, idx);
        user.style.color = str2color(msg.m.substr(0, idx));
        text.innerText = msg.m.substr(idx + 2);
    } else {
        user.innerText = 'System';
        user.style.color = '#999';
        text.innerText = msg.m;
    }

    if (dateDate != todayISO) {
       date.innerText = `${dateTime} ${dateDate}`;
    } else {
       date.innerText = `${dateTime}`;
    }

    msgdiv.appendChild(root);

    return {
        dom: {
            root,
            info,
            user,
            date,
            text
        },
        idf: `${msg.t}_${msg.h}`,
    };
}

let config = {
    sender: ""
};

let messages = {};

let ws;
let reconnectTimeout = null;

function loadChat(data) {
    if (data.name && config.sender.length < 1) {
        config.sender = data.name;
    }

    for (const msg of data.msg) {
        if (msg.type === "channel_msg") {
            addMessage(msg.data);
        }
    }
}

function meshExec(cmds) {
    console.log(`exec: `, cmds);
    fetch("/exec", {
        method: "POST",
        body: JSON.stringify({
            commands: cmds,
        }),
        headers: {
            "Content-type": "application/json; charset=UTF-8"
        }
    })
    .then((response) => response.json())
    .then((json) => console.log(json));
}

window.onload = function(e){ 
    fetchChat();
    connectWs();
}

document.addEventListener("visibilitychange", () => {
    if (document.visibilityState === "visible") {
        checkWebSocket();
    }
});

function checkWebSocket() {
    if (!ws || ws.readyState !== WebSocket.OPEN) {
        scheduleReconnect();
        return;
    }

    try {
        ws.send(JSON.stringify({ type: "ping" }));
    } catch {
        scheduleReconnect();
    }
}

function connectWs() {
    ws = new WebSocket(`ws://${window.location.host}/ws`);
    ws.onopen = function() {
        console.log("WebSocket connected");
        fetchChat();
    };
    ws.onmessage = function(event) {
        console.log("WebSocket message: " + event.data);
        const data = JSON.parse(event.data);
        if (data.type === "channel_msg") {
            addMessage(data.data);
        }
    };
    ws.onclose = function() {
        console.log("WebSocket closed");
        scheduleReconnect();
    };
    ws.onerror = function(error) {
        console.log(error);
        scheduleReconnect();
    };
}

function scheduleReconnect() {
    if (reconnectTimeout) return;

    reconnectTimeout = setTimeout(() => {
        console.log("Reconnecting WebSocket...");
        reconnectTimeout = null;
        connectWs();
    }, 3000);
}

function send() {
    const inp = document.getElementById("chat"); 
    const txt = inp.value;

    if (txt.trim().length) {
        if (config.sender.length) {
            meshExec([`public_raw ${config.sender}: ${txt}`]);
        } else {
            meshExec([`public ${txt}`]);
        }
        inp.value = "";
    }
}

const input = document.getElementById("chat");
chat.onkeydown = (e) => {
    if (e.key == 'Enter') {
        send();
    }
}

document.getElementById("btnsend").onclick = (e) => {
    send();
}

</script>
</html>
)";
