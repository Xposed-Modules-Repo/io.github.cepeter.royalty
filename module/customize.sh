# ============================================================ #
#  customize.sh — runs during module installation             #
#  Does NOT set SKIPUNZIP — APatch auto-extracts the ZIP.      #
# ============================================================ #

# --- Verify MeowZygisk is installed ---
if [ ! -d /data/adb/modules/rezygisk ]; then
    ui_print "- ⚠️  MeowZygisk is not installed."
    ui_print "  Please install MeowZygisk first, then flash this module."
    abort
fi

ui_print "- ✅ MeowZygisk detected"

ui_print "- Installing Telegram Chat Hider module..."
ui_print "- Module ID: telegram_chat_hider"

MODDIR=${0%/*}

# --- Set up config directory ---
# Create default config if it doesn't exist (0600 for privacy)
if [ ! -f "$MODDIR/chat_hider.json" ]; then
    echo '{
  "selected_dialogs": [],
  "hide_in_list": true,
  "hide_in_search": false,
  "hide_in_share": false,
  "hide_in_notifications": false
}' > "$MODDIR/chat_hider.json"
    ui_print "- Created default config"
fi

# --- Set permissions ---
# Config file: private (root only) — 0600 so no other process can
# read which chats are hidden or harvest the dialog catalog.
chmod 0755 "$MODDIR"
chmod 0600 "$MODDIR/chat_hider.json"
# Remove stale dialogs.json if it exists (we now use a Unix socket)
rm -f "$MODDIR/dialogs.json" 2>/dev/null || true

chmod 0755 "$MODDIR/zygisk"
chmod 0755 "$MODDIR/zygisk/arm64-v8a" 2>/dev/null || true
chmod 0755 "$MODDIR/zygisk/armeabi-v7a" 2>/dev/null || true
chmod 0755 "$MODDIR/zygisk/arm64-v8a/libtelegram_hider.so" 2>/dev/null || true
chmod 0755 "$MODDIR/zygisk/armeabi-v7a/libtelegram_hider.so" 2>/dev/null || true

# --- WebUI ---
chmod 0755 "$MODDIR/webroot"
chmod 0644 "$MODDIR/webroot/index.html"
chmod 0644 "$MODDIR/webroot/style.css"
chmod 0755 "$MODDIR/webroot/js" 2>/dev/null || true
chmod 0644 "$MODDIR/webroot/js/app.js"

ui_print "- ✅ Installation complete"
ui_print "- Reboot your device to activate the module."
