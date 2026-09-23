# ============================================================ #
#  customize.sh — runs during module installation             #
# ============================================================ #

SKIPUNZIP=1

# --- Verify MeowZygisk is installed ---
if [ ! -d /data/adb/modules/rezygisk ]; then
    ui_print "- ⚠️  MeowZygisk is not installed."
    ui_print "  Please install MeowZygisk first, then flash this module."
    abort
fi

ui_print "- ✅ MeowZygisk detected"

# --- Verify APatch environment ---
if [ "$APATCH" != "true" ]; then
    ui_print "- ⚠️  This module is designed for APatch."
    ui_print "  It may also work with Magisk or KernelSU."
fi

ui_print "- 🔧 Installing Telegram Chat Hider module..."
ui_print "- Module ID: telegram_chat_hider"

# --- Copy zygisk libraries ---
MODDIR=${0%/*}
ui_print "- Copying Zygisk hooks..."

# Libraries are placed at zygisk/<arch>.so by the ZIP
# No action needed — they are already in the right place after unzip

# --- Set up config directory ---
ui_print "- Setting up configuration..."

mkdir -p "$MODDIR"
# Create default config if it doesn't exist
if [ ! -f "$MODDIR/chat_hider.json" ]; then
    echo '{
  "selected_dialogs": [],
  "hide_in_list": true,
  "hide_in_search": true,
  "hide_in_share": true,
  "hide_in_notifications": true
}' > "$MODDIR/chat_hider.json"
    ui_print "- Created default config"
fi

# --- Set permissions ---
chmod 0755 "$MODDIR"
chmod 0644 "$MODDIR/chat_hider.json"
chmod 0755 "$MODDIR/zygisk"
chmod 0755 "$MODDIR/zygisk/arm64-v8a" 2>/dev/null
chmod 0755 "$MODDIR/zygisk/armeabi-v7a" 2>/dev/null
chmod 0755 "$MODDIR/zygisk/arm64-v8a/libtelegram_hider.so" 2>/dev/null
chmod 0755 "$MODDIR/zygisk/armeabi-v7a/libtelegram_hider.so" 2>/dev/null

# --- WebUI ---
chmod 0755 "$MODDIR/webroot" 2>/dev/null
chmod 0644 "$MODDIR/webroot/index.html" 2>/dev/null
chmod 0644 "$MODDIR/webroot/style.css" 2>/dev/null
chmod 0755 "$MODDIR/webroot/js" 2>/dev/null
chmod 0644 "$MODDIR/webroot/js/app.js" 2>/dev/null

ui_print "- ✅ Installation complete"
ui_print "- Reboot your device to activate the module."
