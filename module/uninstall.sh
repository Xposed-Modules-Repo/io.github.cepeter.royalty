#!/system/bin/sh
# ============================================================ #
#  uninstall.sh — runs when the module is removed             #
# ============================================================ #

MODDIR=${0%/*}

# Clean up config file (optional — keep it so reinstall remembers settings)
# rm -f "$MODDIR/chat_hider.json"

exit 0
