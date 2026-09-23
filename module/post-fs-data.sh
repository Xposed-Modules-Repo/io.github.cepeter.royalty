#!/system/bin/sh
# ============================================================ #
#  post-fs-data.sh — runs early in boot, before /data mount   #
# ============================================================ #

# Nothing needed here for this module — all hooks are in the
# Zygisk .so which is auto-loaded by MeowZygisk.
# This file is required for APatch to recognize the module
# as having early-init scripts.

MODDIR=${0%/*}
exit 0
