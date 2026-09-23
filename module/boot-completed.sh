#!/system/bin/sh
# ============================================================ #
#  boot-completed.sh — runs after Android boots, as root      #
# ============================================================ #

MODDIR=${0%/*}

# Ensure config file permissions are correct after every boot
if [ -f "$MODDIR/chat_hider.json" ]; then
    chown root:root "$MODDIR/chat_hider.json" 2>/dev/null
    chmod 0644 "$MODDIR/chat_hider.json" 2>/dev/null
fi

# Ensure zygisk library permissions
find "$MODDIR/zygisk" -name "*.so" -exec chmod 0755 {} \; 2>/dev/null

exit 0
