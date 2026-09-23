#!/system/bin/sh
# ============================================================ #
#  uninstall.sh — runs when the module is removed               #
#  Wipes privacy-sensitive artifacts (config + socket).       #
# ============================================================ #

MODDIR=${0%/*}

# Securely remove the hidden-chat config — this is the primary
# privacy artifact: the list of which chats the user hid.
rm -f "$MODDIR/chat_hider.json" 2>/dev/null

# Remove any stale socket file (Telegram should unlink on exit, but
# if the process was killed the file may persist on disk).
rm -f "$MODDIR/chat_hider.sock" 2>/dev/null

exit 0
