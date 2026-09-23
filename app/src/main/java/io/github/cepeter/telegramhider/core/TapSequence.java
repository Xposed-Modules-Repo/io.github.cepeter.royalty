package io.github.cepeter.telegramhider.core;

public final class TapSequence {
    private final int requiredTaps;
    private final long maxIntervalMs;

    private int tapCount;
    private long lastTapTime = Long.MIN_VALUE;

    public TapSequence(int requiredTaps, long maxIntervalMs) {
        if (requiredTaps < 2) {
            throw new IllegalArgumentException("requiredTaps must be at least 2");
        }
        if (maxIntervalMs <= 0) {
            throw new IllegalArgumentException("maxIntervalMs must be positive");
        }
        this.requiredTaps = requiredTaps;
        this.maxIntervalMs = maxIntervalMs;
    }

    public synchronized boolean record(long eventTimeMs) {
        if (eventTimeMs < 0 || eventTimeMs <= lastTapTime) {
            return false;
        }

        if (lastTapTime == Long.MIN_VALUE || eventTimeMs - lastTapTime > maxIntervalMs) {
            tapCount = 1;
        } else {
            tapCount++;
        }
        lastTapTime = eventTimeMs;

        if (tapCount < requiredTaps) {
            return false;
        }
        tapCount = 0;
        return true;
    }
}
