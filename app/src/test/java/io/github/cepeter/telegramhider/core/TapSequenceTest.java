package io.github.cepeter.telegramhider.core;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public final class TapSequenceTest {
    @Test
    public void togglesOnFiveDistinctRapidTaps() {
        TapSequence sequence = new TapSequence(5, 800);

        assertFalse(sequence.record(1000));
        assertFalse(sequence.record(1100));
        assertFalse(sequence.record(1200));
        assertFalse(sequence.record(1300));
        assertTrue(sequence.record(1400));
        assertFalse(sequence.record(1500));
    }

    @Test
    public void timeoutRestartsSequence() {
        TapSequence sequence = new TapSequence(5, 800);
        sequence.record(1000);
        sequence.record(1100);
        sequence.record(1200);
        sequence.record(2101);
        sequence.record(2200);
        sequence.record(2300);
        sequence.record(2400);

        assertTrue(sequence.record(2500));
    }

    @Test
    public void duplicateAndBackwardsTimesDoNotCount() {
        TapSequence sequence = new TapSequence(2, 800);

        assertFalse(sequence.record(1000));
        assertFalse(sequence.record(1000));
        assertFalse(sequence.record(999));
        assertTrue(sequence.record(1100));
    }
}
