package io.github.cepeter.telegramhider.xposed;

import android.content.Context;
import android.view.MotionEvent;
import android.widget.Toast;
import de.robv.android.xposed.IXposedHookLoadPackage;
import de.robv.android.xposed.XC_MethodHook;
import de.robv.android.xposed.XposedBridge;
import de.robv.android.xposed.XposedHelpers;
import de.robv.android.xposed.callbacks.XC_LoadPackage;
import io.github.cepeter.telegramhider.core.DialogFilter;
import io.github.cepeter.telegramhider.core.DialogKey;
import io.github.cepeter.telegramhider.core.HiddenConfig;
import io.github.cepeter.telegramhider.core.TapSequence;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicBoolean;

public final class TelegramHook implements IXposedHookLoadPackage {
    private static final long CATALOG_PUBLISH_INTERVAL_MS = 3000;
    private static final int MAX_CATALOG_ENTRIES = 512;

    private static final XposedConfigRepository CONFIG = new XposedConfigRepository();
    private static final AtomicBoolean REVEALED = new AtomicBoolean(false);
    private static final TapSequence TAP_SEQUENCE = new TapSequence(5, 800);
    private static final Map<String, HookStatus> HOOK_STATUS = new LinkedHashMap<>();
    private static final Map<Integer, Long> LAST_CATALOG_PUBLISH = new LinkedHashMap<>();

    private static volatile CatalogPublisher publisher;
    private static ClassLoader telegramClassLoader;

    @Override
    public void handleLoadPackage(XC_LoadPackage.LoadPackageParam lpparam) {
        if (!"org.telegram.messenger".equals(lpparam.packageName)
                || !lpparam.packageName.equals(lpparam.processName)) {
            return;
        }

        telegramClassLoader = lpparam.classLoader;
        install("bridge", () -> installApplicationBridge(lpparam.classLoader));
        install("dialogs", () -> installDialogHook(lpparam.classLoader));
        install("notifications", () -> installNotificationHook(lpparam.classLoader));
        install("reveal", () -> installRevealHook(lpparam.classLoader));
    }

    private static void installApplicationBridge(ClassLoader classLoader) {
        XposedHelpers.findAndHookMethod(
                "org.telegram.messenger.ApplicationLoader",
                classLoader,
                "onCreate",
                new XC_MethodHook() {
                    @Override
                    protected void afterHookedMethod(MethodHookParam param) {
                        try {
                            publisher = new CatalogPublisher((Context) param.thisObject);
                            flushStatus();
                        } catch (RuntimeException error) {
                            XposedBridge.log("TelegramChatHider: bridge startup failed: " + error);
                        }
                    }
                });
    }

    private static void installDialogHook(ClassLoader classLoader) {
        XposedHelpers.findAndHookMethod(
                "org.telegram.messenger.MessagesController",
                classLoader,
                "getDialogs",
                int.class,
                new XC_MethodHook() {
                    @Override
                    protected void afterHookedMethod(MethodHookParam param) {
                        Object rawResult = param.getResult();
                        if (!(rawResult instanceof List<?>)) {
                            return;
                        }

                        try {
                            int account = XposedHelpers.getIntField(param.thisObject, "currentAccount");
                            @SuppressWarnings("unchecked")
                            List<Object> source = (List<Object>) rawResult;
                            publishCatalogIfDue(account, param.thisObject, source);

                            HiddenConfig config = CONFIG.current();
                            List<Object> filtered = DialogFilter.filteredCopy(
                                    source,
                                    dialog -> DialogKey.of(
                                            account,
                                            XposedHelpers.getLongField(dialog, "id")),
                                    config,
                                    REVEALED.get());
                            param.setResult(filtered);
                        } catch (Throwable error) {
                            reportRuntimeError("dialogs", error);
                        }
                    }
                });
    }

    private static void installNotificationHook(ClassLoader classLoader) {
        XposedHelpers.findAndHookMethod(
                "org.telegram.messenger.NotificationsController",
                classLoader,
                "processNewMessages",
                ArrayList.class,
                boolean.class,
                boolean.class,
                CountDownLatch.class,
                new XC_MethodHook() {
                    @Override
                    protected void beforeHookedMethod(MethodHookParam param) {
                        if (!(param.args[0] instanceof List<?>)) {
                            return;
                        }

                        try {
                            HiddenConfig config = CONFIG.current();
                            if (!config.suppressNotifications()) {
                                return;
                            }
                            int account = XposedHelpers.getIntField(param.thisObject, "currentAccount");
                            @SuppressWarnings("unchecked")
                            List<Object> source = (List<Object>) param.args[0];
                            List<Object> filtered = DialogFilter.filteredCopy(
                                    source,
                                    message -> DialogKey.of(
                                            account,
                                            ((Number) XposedHelpers.callMethod(
                                                    message, "getDialogId")).longValue()),
                                    config,
                                    false);
                            param.args[0] = filtered;
                        } catch (Throwable error) {
                            reportRuntimeError("notifications", error);
                        }
                    }
                });
    }

    private static void installRevealHook(ClassLoader classLoader) {
        XposedHelpers.findAndHookMethod(
                "org.telegram.ui.ActionBar.ActionBar",
                classLoader,
                "dispatchTouchEvent",
                MotionEvent.class,
                new XC_MethodHook() {
                    @Override
                    protected void beforeHookedMethod(MethodHookParam param) {
                        MotionEvent event = (MotionEvent) param.args[0];
                        if (event == null || event.getActionMasked() != MotionEvent.ACTION_DOWN) {
                            return;
                        }

                        try {
                            Object fragment = XposedHelpers.getObjectField(
                                    param.thisObject, "parentFragment");
                            if (fragment == null
                                    || !"org.telegram.ui.DialogsActivity".equals(
                                            fragment.getClass().getName())
                                    || !TAP_SEQUENCE.record(event.getDownTime())) {
                                return;
                            }

                            boolean revealed = toggleReveal();
                            Context context = ((android.view.View) param.thisObject).getContext();
                            Toast.makeText(
                                            context,
                                            revealed
                                                    ? "Hidden chats revealed"
                                                    : "Hidden chats concealed",
                                            Toast.LENGTH_SHORT)
                                    .show();
                            requestDialogsReload(fragment);
                        } catch (Throwable error) {
                            reportRuntimeError("reveal", error);
                        }
                    }
                });
    }

    private static void publishCatalogIfDue(
            int account, Object messagesController, List<Object> dialogs) {
        CatalogPublisher currentPublisher = publisher;
        if (currentPublisher == null) {
            return;
        }

        long now = android.os.SystemClock.elapsedRealtime();
        synchronized (LAST_CATALOG_PUBLISH) {
            Long last = LAST_CATALOG_PUBLISH.get(account);
            if (last != null && now - last < CATALOG_PUBLISH_INTERVAL_MS) {
                return;
            }
            LAST_CATALOG_PUBLISH.put(account, now);
        }

        int count = Math.min(dialogs.size(), MAX_CATALOG_ENTRIES);
        long[] ids = new long[count];
        String[] titles = new String[count];
        int added = 0;
        for (int index = 0; index < count; index++) {
            Object dialog = dialogs.get(index);
            try {
                long id = XposedHelpers.getLongField(dialog, "id");
                if (id == 0) {
                    continue;
                }
                ids[added] = id;
                titles[added] = resolveDialogTitle(messagesController, id);
                added++;
            } catch (Throwable ignored) {
                // Telegram may add synthetic rows; they are not hideable dialogs.
            }
        }
        if (added != count) {
            ids = java.util.Arrays.copyOf(ids, added);
            titles = java.util.Arrays.copyOf(titles, added);
        }
        currentPublisher.submit(account, ids, titles);
    }

    private static String resolveDialogTitle(Object messagesController, long dialogId) {
        try {
            if (dialogId > 0) {
                Object user = XposedHelpers.callMethod(
                        messagesController, "getUser", Long.valueOf(dialogId));
                if (user != null) {
                    String firstName = nullableString(XposedHelpers.getObjectField(user, "first_name"));
                    String lastName = nullableString(XposedHelpers.getObjectField(user, "last_name"));
                    String fullName = (firstName + " " + lastName).trim();
                    if (!fullName.isEmpty()) {
                        return fullName;
                    }
                    String username = nullableString(XposedHelpers.getObjectField(user, "username"));
                    if (!username.isEmpty()) {
                        return "@" + username;
                    }
                }
            } else {
                Object chat = XposedHelpers.callMethod(
                        messagesController, "getChat", Long.valueOf(-dialogId));
                if (chat != null) {
                    String title = nullableString(XposedHelpers.getObjectField(chat, "title"));
                    if (!title.isEmpty()) {
                        return title;
                    }
                }
            }
        } catch (Throwable ignored) {
            // ID fallback is stable across Telegram schema changes.
        }
        return String.valueOf(dialogId);
    }

    private static String nullableString(Object value) {
        return value instanceof String ? (String) value : "";
    }

    private static boolean toggleReveal() {
        for (;;) {
            boolean current = REVEALED.get();
            boolean next = !current;
            if (REVEALED.compareAndSet(current, next)) {
                return next;
            }
        }
    }

    private static void requestDialogsReload(Object fragment) {
        int account = XposedHelpers.getIntField(fragment, "currentAccount");
        Class<?> notificationCenter = XposedHelpers.findClass(
                "org.telegram.messenger.NotificationCenter", telegramClassLoader);
        Object instance = XposedHelpers.callStaticMethod(
                notificationCenter, "getInstance", account);
        int event = XposedHelpers.getStaticIntField(notificationCenter, "dialogsNeedReload");
        XposedHelpers.callMethod(instance, "postNotificationName", event, new Object[0]);
    }

    private static void install(String hook, HookInstaller installer) {
        try {
            installer.install();
            reportStatus(hook, "installed", "");
        } catch (Throwable error) {
            reportStatus(hook, "missing", error.getClass().getSimpleName());
            XposedBridge.log("TelegramChatHider: " + hook + " hook unavailable: " + error);
        }
    }

    private static void reportRuntimeError(String hook, Throwable error) {
        if (error instanceof VirtualMachineError) {
            throw (VirtualMachineError) error;
        }
        reportStatus(hook, "runtime_error", error.getClass().getSimpleName());
        XposedBridge.log("TelegramChatHider: " + hook + " runtime error: " + error);
    }

    private static void reportStatus(String hook, String status, String detail) {
        synchronized (HOOK_STATUS) {
            HOOK_STATUS.put(hook, new HookStatus(hook, status, detail));
        }
        CatalogPublisher currentPublisher = publisher;
        if (currentPublisher != null) {
            currentPublisher.reportStatus(hook, status, detail);
        }
    }

    private static void flushStatus() {
        CatalogPublisher currentPublisher = publisher;
        if (currentPublisher == null) {
            return;
        }
        synchronized (HOOK_STATUS) {
            for (HookStatus status : HOOK_STATUS.values()) {
                currentPublisher.reportStatus(status.hook, status.status, status.detail);
            }
        }
    }

    private interface HookInstaller {
        void install() throws Throwable;
    }

    private static final class HookStatus {
        final String hook;
        final String status;
        final String detail;

        HookStatus(String hook, String status, String detail) {
            this.hook = hook;
            this.status = status;
            this.detail = detail;
        }
    }
}
