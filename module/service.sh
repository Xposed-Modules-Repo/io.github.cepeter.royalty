#!/system/bin/sh
# ============================================================ #
#  service.sh — runs in late_start service mode               #
# ============================================================ #

MODDIR=${0%/*}

# This module uses MeowZygisk's Zygisk injection, so no daemon
# is needed.  All hooking happens in the Zygote before app
# specialization.

# Keep config readable for the WebUI
if [ -f "$MODDIR/chat_hider.json" ]; then
    chmod 0644 "$MODDIR/chat_hider.json" 2>/dev/null
fi

exit 0
