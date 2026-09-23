package io.github.cepeter.telegramhider.xposed;

import de.robv.android.xposed.IXposedHookLoadPackage;
import de.robv.android.xposed.callbacks.XC_LoadPackage;

public final class TelegramHook implements IXposedHookLoadPackage {
    private static final String TELEGRAM_PACKAGE = "org.telegram.messenger";

    @Override
    public void handleLoadPackage(XC_LoadPackage.LoadPackageParam loadPackageParam) {
        if (!TELEGRAM_PACKAGE.equals(loadPackageParam.packageName)
                || !TELEGRAM_PACKAGE.equals(loadPackageParam.processName)) {
            return;
        }
        // Hook installation is implemented after the pure filtering contracts.
    }
}
