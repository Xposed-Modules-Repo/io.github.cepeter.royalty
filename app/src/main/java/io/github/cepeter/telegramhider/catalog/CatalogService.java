package io.github.cepeter.telegramhider.catalog;

import android.app.Service;
import android.content.Intent;
import android.os.Binder;
import android.os.IBinder;
import io.github.cepeter.telegramhider.ICatalogService;

public final class CatalogService extends Service {
    private static final String TELEGRAM_PACKAGE = "org.telegram.messenger";
    private static final int MAX_STATUS_NAME_LENGTH = 32;
    private static final int MAX_STATUS_DETAIL_LENGTH = 256;

    private CatalogRepository repository;

    private final ICatalogService.Stub binder = new ICatalogService.Stub() {
        @Override
        public void submit(int account, long[] ids, String[] titles) {
            verifyTelegramCaller();
            if (!repository.replaceAccount(account, ids, titles)) {
                throw new IllegalStateException("catalog persistence failed");
            }
        }

        @Override
        public void reportStatus(String hook, String status, String detail) {
            verifyTelegramCaller();
            String safeHook = requireStatusToken(hook, "hook");
            String safeStatus = requireStatusToken(status, "status");
            String safeDetail = detail == null ? "" : detail;
            if (safeDetail.length() > MAX_STATUS_DETAIL_LENGTH) {
                safeDetail = safeDetail.substring(0, MAX_STATUS_DETAIL_LENGTH);
            }
            if (!repository.recordStatus(safeHook, safeStatus, safeDetail)) {
                throw new IllegalStateException("status persistence failed");
            }
        }
    };

    @Override
    public void onCreate() {
        super.onCreate();
        repository = new CatalogRepository(this);
    }

    @Override
    public IBinder onBind(Intent intent) {
        return binder;
    }

    private void verifyTelegramCaller() {
        int uid = Binder.getCallingUid();
        String[] packages = getPackageManager().getPackagesForUid(uid);
        if (packages != null) {
            for (String packageName : packages) {
                if (TELEGRAM_PACKAGE.equals(packageName)) {
                    return;
                }
            }
        }
        throw new SecurityException("caller uid " + uid + " is not " + TELEGRAM_PACKAGE);
    }

    private static String requireStatusToken(String value, String name) {
        if (value == null || value.isEmpty() || value.length() > MAX_STATUS_NAME_LENGTH) {
            throw new IllegalArgumentException(name + " has invalid length");
        }
        for (int index = 0; index < value.length(); index++) {
            char character = value.charAt(index);
            if (!((character >= 'a' && character <= 'z') || character == '_')) {
                throw new IllegalArgumentException(name + " contains invalid characters");
            }
        }
        return value;
    }
}
