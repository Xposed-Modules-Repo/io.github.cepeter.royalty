/*
 * Telegram Chat Hider — WebUI JavaScript
 *
 * Communicates with the host via window.android.exec() (APatch root bridge).
 * Dialog catalog is fetched via Unix domain socket (no plaintext file on disk).
 * Config is read/written with 0600 permissions.
 */

const MOD_DIR = "/data/adb/modules/telegram_chat_hider";
const CONFIG_FILE = MOD_DIR + "/chat_hider.json";
const SOCK_FILE = MOD_DIR + "/chat_hider.sock";

/* ── Root command execution ────────────────────────────── */

function exec(cmd, callback) {
    if (typeof window.android !== 'undefined' && callback) {
        window.android.exec(cmd, null, callback);
    } else if (typeof window.android !== 'undefined') {
        return window.android.exec(cmd);
    } else {
        // Debug fallback
        console.log("[mock exec]", cmd);
        if (callback) callback(0, "", "");
        return "mock";
    }
}

/* ── JSON config helpers ───────────────────────────────── */

const DEFAULT_CONFIG = {
    selected_dialogs: [],
    hide_in_list: true,
    hide_in_search: false,
    hide_in_share: false,
    hide_in_notifications: false
};

function loadConfig(callback) {
    const cmd = "cat '" + CONFIG_FILE + "' 2>/dev/null || echo '{}' ";
    exec(cmd, function(code, stdout, stderr) {
        let cfg;
        try {
            const raw = stdout.trim();
            cfg = raw ? JSON.parse(raw) : {};
        } catch(e) {
            cfg = {};
        }
        // Merge with defaults, normalize selected_dialogs to strings
        const result = Object.assign({}, DEFAULT_CONFIG, cfg);
        result.selected_dialogs = (cfg.selected_dialogs || []).map(function(id) {
            return String(id);
        });
        callback(result);
    });
}

function saveConfig(cfg, callback) {
    const json = JSON.stringify(cfg, null, 2);
    /* Write config with 0600 (owner-only) permissions.
     * Use printf to avoid heredoc/newline escaping issues. */
    const jsonEscaped = String(json).replace(/'/g, "'\\''");
    const cmd = "printf '%s' '" + jsonEscaped + "' > '" + CONFIG_FILE + "' && chmod 0600 '" + CONFIG_FILE + "'";
    exec(cmd, function(code, stdout, stderr) {
        callback(code === 0 || code === "0");
    });
}

/* ── Dialog catalog via Unix domain socket ─────────────── */

/*
 * The native Zygisk module listens on a Unix domain socket (SOCK_PATH).
 * It requires SO_PEERCRED uid=0 (root) — the socket is chmod 0600.
 * On connection, the server sends a JSON array of dialog objects:
 *   [{"id":"1234567890123456789"}]
 * If Telegram is not running, the socket doesn't exist and we show a hint.
 */
function loadDialogs(callback) {
    // Try connecting to the Unix socket first
    // Format: echo 'GET_DIALOGS' | nc -U <socket>
    // The server sends: <8-digit-hex-length>\n<json-data>\n
    const cmd =
        "if [ -S '" + SOCK_FILE + "' ]; then " +
        "  nc -U '" + SOCK_FILE + "' </dev/null 2>/dev/null " +
        "  | head -c 1048576" +
        "; else echo 'SOCKET_OFFLINE'; fi";

    exec(cmd, function(code, stdout, stderr) {
        let dialogs = [];
        const raw = stdout.trim();

        if (raw === 'SOCKET_OFFLINE') {
            // Socket doesn't exist — Telegram not running
            callback([], true);  // second arg = offline
            return;
        }

        try {
            // Response format: <8-hex-digits>\n<json>\n
            // But nc may merge them; parse what we get
            const jsonStart = raw.indexOf('[');
            if (jsonStart >= 0) {
                const jsonStr = raw.substring(jsonStart);
                const parsed = JSON.parse(jsonStr);
                if (Array.isArray(parsed)) dialogs = parsed;
                else if (parsed.dialogs) dialogs = parsed.dialogs;
            }
        } catch(e) {
            dialogs = [];
        }
        callback(dialogs, false);
    });
}

/* ── Status checks ─────────────────────────────────────── */

function checkStatus() {
    // Check APatch (reliable: check for /data/adb/apatch directory)
    exec("ls /data/adb/apatch >/dev/null 2>&1 && echo true || echo none",
        function(code, stdout) {
            const ok = stdout.trim() === "true";
            setStatus("status-apatch", ok ? "ok" : "error", ok ? "Running" : "Not detected");
        });

    // Check MeowZygisk
    exec("ls /data/adb/modules/rezygisk/module.prop 2>/dev/null && echo found || echo missing",
        function(code, stdout) {
            const ok = stdout.trim().includes("found");
            setStatus("status-meowzygisk", ok ? "ok" : "error", ok ? "Installed" : "Not found");
        });

    // Check Telegram
    exec("pm list packages org.telegram.messenger 2>/dev/null || echo missing",
        function(code, stdout) {
            const ok = stdout.trim().includes("org.telegram.messenger");
            setStatus("status-telegram", ok ? "ok" : "error", ok ? "Installed" : "Not found");
        });

    // Check module installed
    exec("ls " + MOD_DIR + "/zygisk 2>/dev/null && echo found || echo missing",
        function(code, stdout) {
            const ok = stdout.trim().includes("found");
            setStatus("status-module", ok ? "ok" : "error", ok ? "Installed" : "Not installed");
        });

    // Check if Telegram is running — probe the socket by actually connecting,
    // not by testing file existence (stale socket persists after exit).
    exec("nc -z -U '" + SOCK_FILE + "' 2>/dev/null && echo online || echo offline",
        function(code, stdout) {
            const online = stdout.trim().includes("online");
            setStatus("status-telegram", online ? "ok" : "error",
                online ? "Active (Telegram running)" : "Not running");
        });
}

function setStatus(id, cls, text) {
    const el = document.getElementById(id);
    if (el) {
        el.className = "status " + cls;
        el.textContent = text;
    }
}

/* ── Render chat list ──────────────────────────────────── */

let allDialogs = [];
let filteredDialogs = [];
let selectedIds = new Set();

function renderChats() {
    const container = document.getElementById("chat-list");
    if (!container) return;

    if (filteredDialogs.length === 0 && !window._chatsLoaded) {
        container.innerHTML =
            '<div class="loading">No chats found. Make sure Telegram is installed and you\'ve ' +
            'opened it at least once so the module can export the dialog catalog.</div>';
        return;
    }

    window._chatsLoaded = true;

    container.innerHTML = filteredDialogs.map(d => {
        const id = String(d.id || d.dialog_id || d.id_str || "");
        const name = d.name || d.title || d.username || ("Chat " + id);
        const type = String(d.type || "");
        const checked = selectedIds.has(id);
        return `<div class="chat-item" onclick="toggleChat('${escapeHtml(id)}')">
            <div class="chat-checkbox ${checked ? 'checked' : ''}"></div>
            <div class="chat-name">${escapeHtml(name)}</div>
            <div class="chat-id">${escapeHtml(id)}${type ? ' · ' + escapeHtml(type) : ''}</div>
        </div>`;
    }).join('');
}

function toggleChat(id) {
    if (selectedIds.has(id)) {
        selectedIds.delete(id);
    } else {
        selectedIds.add(id);
    }
    renderChats();
}

function escapeHtml(s) {
    return String(s || "").replace(/[&<>"']/g, c => {
        const m = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' };
        return m[c];
    });
}

/* ── Search filter ─────────────────────────────────────── */

function filterChats() {
    const q = document.getElementById("chat-search").value.toLowerCase();
    filteredDialogs = allDialogs.filter(d => {
        const name = String(d.name || d.title || d.username || "").toLowerCase();
        const id = String(d.id || d.dialog_id || d.id_str || "").toLowerCase();
        return name.includes(q) || id.includes(q);
    });
    renderChats();
}

/* ── Save configuration ────────────────────────────────── */

function saveConfiguration() {
    const cfg = {
        selected_dialogs: Array.from(selectedIds),
        hide_in_list: document.getElementById("toggle-list").checked,
        hide_in_search: document.getElementById("toggle-search").checked,
        hide_in_share: document.getElementById("toggle-share").checked,
        hide_in_notifications: document.getElementById("toggle-notifications").checked,
    };

    saveConfig(cfg, function(ok) {
        if (ok) {
            alert("Configuration saved! Restart Telegram for changes to take effect.");
        } else {
            alert("Failed to save configuration. Check log for details.");
        }
    });
}

/* ── Init ──────────────────────────────────────────────── */

document.addEventListener("DOMContentLoaded", function() {
    // Wire up events
    document.getElementById("btn-refresh").addEventListener("click", checkStatus);
    document.getElementById("btn-save").addEventListener("click", saveConfiguration);
    document.getElementById("chat-search").addEventListener("input", filterChats);

    // Load everything
    loadConfig(function(cfg) {
        document.getElementById("toggle-list").checked = cfg.hide_in_list;
        document.getElementById("toggle-search").checked = cfg.hide_in_search;
        document.getElementById("toggle-share").checked = cfg.hide_in_share;
        document.getElementById("toggle-notifications").checked = cfg.hide_in_notifications;
        selectedIds = new Set(cfg.selected_dialogs);
    });

    loadDialogs(function(dialogs, offline) {
        if (offline) {
            window._chatsLoaded = false;
            document.getElementById("chat-list").innerHTML =
                '<div class="loading">Telegram not running. Open Telegram once, then refresh.</div>';
        } else {
            allDialogs = dialogs;
            filteredDialogs = dialogs;
            renderChats();
        }
    });

    checkStatus();
});