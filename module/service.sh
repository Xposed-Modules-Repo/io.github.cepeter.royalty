#!/system/bin/sh
# ============================================================ #
#  service.sh — runs in late_start service mode               #
# ============================================================ #

MODDIR=${0%/*}

# This module uses MeowZygisk's Zygisk injection, so no daemon
# is needed.  All hooking happens in the Zygote before app
# specialization.

# Config stays 0600 — only root/Telegram process should access it
if [ -f "$MODDIR/chat_hider.json" ]; then
    chmod 0600 "$MODDIR/chat_hider.json" 2>/dev/null
fi

exit 0
