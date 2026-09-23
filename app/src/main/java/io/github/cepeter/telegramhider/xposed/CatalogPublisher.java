package io.github.cepeter.telegramhider.xposed;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.IBinder;
import android.os.RemoteException;
import de.robv.android.xposed.XposedBridge;
import io.github.cepeter.telegramhider.BuildConfig;
import io.github.cepeter.telegramhider.ICatalogService;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.Map;

public final class CatalogPublisher {
    private static final String SERVICE_CLASS =
            "io.github.cepeter.telegramhider.catalog.CatalogService";

    private final Context context;
    private final Map<Integer, CatalogBatch> pendingCatalogs = new LinkedHashMap<>();
    private final Map<String, Status> pendingStatuses = new LinkedHashMap<>();
    private ICatalogService service;
    private boolean bindRequested;

    private final ServiceConnection connection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder binder) {
            synchronized (CatalogPublisher.this) {
                service = ICatalogService.Stub.asInterface(binder);
                bindRequested = true;
                flushLocked();
            }
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            synchronized (CatalogPublisher.this) {
                service = null;
                bindRequested = false;
            }
        }

        @Override
        public void onBindingDied(ComponentName name) {
            onServiceDisconnected(name);
        }

        @Override
        public void onNullBinding(ComponentName name) {
            onServiceDisconnected(name);
        }
    };

    public CatalogPublisher(Context context) {
        this.context = context.getApplicationContext();
    }

    public synchronized void submit(int account, long[] ids, String[] titles) {
        pendingCatalogs.put(account, new CatalogBatch(
                account,
                Arrays.copyOf(ids, ids.length),
                Arrays.copyOf(titles, titles.length)));
        ensureBoundLocked();
        flushLocked();
    }

    public synchronized void reportStatus(String hook, String status, String detail) {
        pendingStatuses.put(hook, new Status(hook, status, detail == null ? "" : detail));
        ensureBoundLocked();
        flushLocked();
    }

    private void ensureBoundLocked() {
        if (service != null || bindRequested) {
            return;
        }
        Intent intent = new Intent().setComponent(
                new ComponentName(BuildConfig.APPLICATION_ID, SERVICE_CLASS));
        try {
            bindRequested = context.bindService(intent, connection, Context.BIND_AUTO_CREATE);
            if (!bindRequested) {
                XposedBridge.log("TelegramChatHider: catalog service bind returned false");
            }
        } catch (RuntimeException error) {
            bindRequested = false;
            XposedBridge.log("TelegramChatHider: catalog service bind failed: " + error);
        }
    }

    private void flushLocked() {
        if (service == null) {
            return;
        }
        try {
            for (CatalogBatch batch : pendingCatalogs.values()) {
                service.submit(batch.account, batch.ids, batch.titles);
            }
            pendingCatalogs.clear();
            for (Status status : pendingStatuses.values()) {
                service.reportStatus(status.hook, status.status, status.detail);
            }
            pendingStatuses.clear();
        } catch (RemoteException | RuntimeException error) {
            service = null;
            bindRequested = false;
            XposedBridge.log("TelegramChatHider: catalog service call failed: " + error);
        }
    }

    private static final class CatalogBatch {
        final int account;
        final long[] ids;
        final String[] titles;

        CatalogBatch(int account, long[] ids, String[] titles) {
            this.account = account;
            this.ids = ids;
            this.titles = titles;
        }
    }

    private static final class Status {
        final String hook;
        final String status;
        final String detail;

        Status(String hook, String status, String detail) {
            this.hook = hook;
            this.status = status;
            this.detail = detail;
        }
    }
}
