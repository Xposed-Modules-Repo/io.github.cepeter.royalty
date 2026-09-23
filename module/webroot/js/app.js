/*
 * Telegram Chat Hider — WebUI JavaScript
 *
 * Communicates with the host via window.android.exec() (APatch root bridge).
 * Reads/writes JSON config files in the module directory.
 */

const MOD_DIR = "/data/adb/modules/telegram_chat_hider";
const CONFIG_FILE = MOD_DIR + "/chat_hider.json";
const DIALOGS_FILE = MOD_DIR + "/dialogs.json";

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
    hide_in_search: true,
    hide_in_share: true,
    hide_in_notifications: true
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
        // Merge with defaults
        const result = Object.assign({}, DEFAULT_CONFIG, cfg);
        callback(result);
    });
}

function saveConfig(cfg, callback) {
    const json = JSON.stringify(cfg, null, 2);
    // Write via a heredoc-like approach
    const cmd = "cat > '" + CONFIG_FILE + "' <<'HEREDOC_END'\n" +
                json + "\nHEREDOC_END\n" +
                "chmod 0644 '" + CONFIG_FILE + "'";
    exec(cmd, function(code, stdout, stderr) {
        callback(code === 0 || code === "0");
    });
}

/* ── Dialog catalog ────────────────────────────────────── */

function loadDialogs(callback) {
    const cmd = "cat '" + DIALOGS_FILE + "' 2>/dev/null || echo '[]'";
    exec(cmd, function(code, stdout, stderr) {
        let dialogs = [];
        try {
            const raw = stdout.trim();
            if (raw) {
                const parsed = JSON.parse(raw);
                if (Array.isArray(parsed)) dialogs = parsed;
                else if (parsed.dialogs) dialogs = parsed.dialogs;
            }
        } catch(e) {
            dialogs = [];
        }
        callback(dialogs);
    });
}

/* ── Status checks ─────────────────────────────────────── */

function checkStatus() {
    // Check APatch
    exec("echo $APATCH 2>/dev/null || getprop ro.kernel.apatch 2>/dev/null || echo none",
        function(code, stdout) {
            const ok = stdout.trim() === "true" || stdout.trim() !== "none";
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

    // Check module
    exec("ls " + MOD_DIR + "/zygisk 2>/dev/null && echo found || echo missing",
        function(code, stdout) {
            const ok = stdout.trim().includes("found");
            setStatus("status-module", ok ? "ok" : "error", ok ? "Active" : "Not installed");
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
        container.innerHTML = '<div class="loading">No chats found. Make sure Telegram is installed and you\'ve opened it at least once.</div>';
        return;
    }

    window._chatsLoaded = true;

    container.innerHTML = filteredDialogs.map(d => {
        const id = d.id || d.dialog_id || d.id_str;
        const name = d.name || d.title || d.username || ("Chat " + id);
        const type = d.type || "";
        const checked = selectedIds.has(id);
        return `<div class="chat-item" onclick="toggleChat('${id}')">
            <div class="chat-checkbox ${checked ? 'checked' : ''}"></div>
            <div class="chat-name">${escapeHtml(name)}</div>
            <div class="chat-id">${escapeHtml(id)}${type ? ' · ' + type : ''}</div>
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
    return s.replace(/[&<>"']/g, c => {
        const m = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' };
        return m[c];
    });
}

/* ── Search filter ─────────────────────────────────────── */

function filterChats() {
    const q = document.getElementById("chat-search").value.toLowerCase();
    filteredDialogs = allDialogs.filter(d => {
        const name = (d.name || d.title || d.username || "").toLowerCase();
        const id = (d.id || d.dialog_id || "").toLowerCase();
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
            alert("Configuration saved! The changes will take effect after restarting Telegram.");
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

    // Toggle event listeners
    ["toggle-list", "toggle-search", "toggle-share", "toggle-notifications"].forEach(id => {
        const el = document.getElementById(id);
        if (el) el.addEventListener("change", function() {
            // Auto-save surface toggles
        });
    });

    // Load everything
    loadConfig(function(cfg) {
        document.getElementById("toggle-list").checked = cfg.hide_in_list;
        document.getElementById("toggle-search").checked = cfg.hide_in_search;
        document.getElementById("toggle-share").checked = cfg.hide_in_share;
        document.getElementById("toggle-notifications").checked = cfg.hide_in_notifications;
        selectedIds = new Set(cfg.selected_dialogs);
    });

    loadDialogs(function(dialogs) {
        allDialogs = dialogs;
        filteredDialogs = dialogs;
        renderChats();
    });

    checkStatus();
});
