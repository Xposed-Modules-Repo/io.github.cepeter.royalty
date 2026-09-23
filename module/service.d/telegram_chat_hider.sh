#!/system/bin/sh
# APatch service script - runs at boot to set up permissions
# This script ensures the module config is accessible to both
# the WebUI daemon and the Zygisk library

MODDIR=${0%/*}
CONFIG_DIR="/data/adb/modules/telegram_chat_hider"

# Ensure config directory exists with proper permissions
mkdir -p "$CONFIG_DIR/config"
chmod 755 "$CONFIG_DIR/config"

# Create default config if it doesn't exist
if [ ! -f "$CONFIG_DIR/config/hidden_chats.json" ]; then
    echo '[]' > "$CONFIG_DIR/config/hidden_chats.json"
    chmod 644 "$CONFIG_DIR/config/hidden_chats.json"
fi

# Create reveal state file (toggled by the 5-tap gesture)
if [ ! -f "$CONFIG_DIR/config/reveal_state" ]; then
    echo 'hidden' > "$CONFIG_DIR/config/reveal_state"
    chmod 644 "$CONFIG_DIR/config/reveal_state"
fi
