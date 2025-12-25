static const char *htmlSettings PROGMEM = R"(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>MeshCore Logger</title>
    <style>
        * {
            padding: 0;
            margin: 0;
            font-family: sans-serif;
        }

        #contents {
            padding: 24px;
        }

        input {
            background: #f8f8f8;
            border: solid 1px #bcbcbc;
            border-radius: 8px;
            padding: 4px;
        }

        button {
            border: solid 1px #a5a5a5;
            padding: 4px;
            border-radius: 7px;
        }

        .input-group {
            margin: 8px 0;
        }
        
        .input-group label span {
            display: inline-block;
            min-width: 160px;
        }

        .input-group label input {
            display: inline-block;
            min-width: 320px;
        }

        .prefs-group {
            margin-bottom: 16px;
        }
    </style>
</head>
<body>
    <div id="contents">
        <h2>Node Preferences</h2>
        <div id="nodePrefs" class="prefs-group"></div>

        <h2>WiFi Preferences</h2>
        <div id="wifiPrefs" class="prefs-group"></div>

        <h2>Logger Preferences</h2>
        <div id="loggerPrefs" class="prefs-group"></div>
    </div>
</body>

<script>
function loadData() {
    const url = "/settings.json";
    fetch(url)
        .then((response) => response.json())
        .then((json) => loadSettings(json));
}

function meshExec(cmds) {
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

function loadSettings(data) {
    console.log(data);
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
    loadData();
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

function str2col(str) {
  let hash = 0;
  for (let i = 0; i < str.length; i++) {
    hash = str.charCodeAt(i) + ((hash << 5) - hash);
  }

  const hue = Math.abs(hash) % 360;
  return `hsl(${hue}, 65%, 55%)`;
}

function loadData() {
    const url = "/chat.json";
    fetch(url)
        .then((response) => response.json())
        .then((json) => loadChat(json));
}

function addMessage(msg) {
    let messages = document.getElementById("messages");

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

    user.innerText = msg.m.substr(0, idx);
    user.style.color = str2col(msg.m.substr(0, idx));

    text.innerText = msg.m.substr(idx + 2);
    if (dateDate != todayISO) {
       date.innerText = `${dateTime} ${dateDate}`;
    } else {
       date.innerText = `${dateTime}`;
    }

    messages.appendChild(root);

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

var config = {
    sender: ""
};

function loadChat(data) {
    if (data["name"]) {
        config.sender = data["name"];
    }

    if (!data["msg"].length) return;

    let messages = document.getElementById("messages");
    messages.innerHTML = '';

    for (const msg of data["msg"]) {
        addMessage(msg);
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
    loadData();
    openWs();
}

function openWs() {
    const ws = new WebSocket(`ws://${window.location.host}/ws`);
    ws.onopen = function() {
        console.log("WebSocket connected");
    };
    ws.onmessage = function(event) {
        console.log("WebSocket message: " + event.data);
        const data = JSON.parse(event.data);
        addMessage(data);
    };
    ws.onclose = function() {
        console.log("WebSocket closed");
    };
    ws.onerror = function(error) {
        console.log("WebSocket error: " + error);
        // todo: reconnect timer: openWs()
    };
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
