package io.github.cepeter.telegramhider;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.TypedValue;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;
import io.github.cepeter.telegramhider.catalog.CatalogProtocol;
import io.github.cepeter.telegramhider.catalog.CatalogRepository;
import io.github.cepeter.telegramhider.catalog.CatalogRequestClient;
import io.github.cepeter.telegramhider.config.ConfigStore;
import io.github.cepeter.telegramhider.core.CatalogEntry;
import io.github.cepeter.telegramhider.core.DialogKey;
import io.github.cepeter.telegramhider.core.HiddenConfig;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

public final class MainActivity extends Activity {
    private final List<CatalogEntry> catalog = new ArrayList<>();
    private boolean preferencesAvailable;
    private boolean catalogUpdatesRegistered;
    private boolean catalogRequestTimedOut;
    private long requestExpiresAt;

    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final Runnable catalogTimeout = () -> {
        if (requestExpiresAt != 0 && SystemClock.elapsedRealtime() >= requestExpiresAt) {
            catalogRequestTimedOut = true;
            requestExpiresAt = 0;
            renderCached();
        }
    };
    private final BroadcastReceiver catalogUpdatedReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            catalogRequestTimedOut = false;
            requestExpiresAt = 0;
            mainHandler.removeCallbacks(catalogTimeout);
            renderCached();
        }
    };

    private CatalogRepository catalogRepository;
    private TextView statusView;
    private ListView dialogList;
    private ArrayAdapter<String> dialogAdapter;
    private Switch notificationSwitch;
    private Button saveButton;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        catalogRepository = new CatalogRepository(this);
        setContentView(buildContentView());
    }

    @Override
    protected void onResume() {
        super.onResume();
        registerCatalogUpdates();
        renderCached();
        requestCatalog();
    }

    @Override
    protected void onPause() {
        mainHandler.removeCallbacks(catalogTimeout);
        if (catalogUpdatesRegistered) {
            unregisterReceiver(catalogUpdatedReceiver);
            catalogUpdatesRegistered = false;
        }
        super.onPause();
    }

    private View buildContentView() {
        int padding = dp(16);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(padding, padding, padding, padding);

        TextView title = new TextView(this);
        title.setText(R.string.app_name);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 24);
        root.addView(title, matchWrap());

        TextView scope = new TextView(this);
        scope.setText(R.string.supported_scope);
        scope.setPadding(0, dp(8), 0, dp(8));
        root.addView(scope, matchWrap());

        statusView = new TextView(this);
        statusView.setTextIsSelectable(true);
        root.addView(statusView, matchWrap());

        Button refreshButton = new Button(this);
        refreshButton.setText(R.string.refresh);
        refreshButton.setOnClickListener(view -> requestCatalog());
        root.addView(refreshButton, matchWrap());

        notificationSwitch = new Switch(this);
        notificationSwitch.setText(R.string.suppress_notifications);
        root.addView(notificationSwitch, matchWrap());

        dialogList = new ListView(this);
        dialogList.setChoiceMode(ListView.CHOICE_MODE_MULTIPLE);
        dialogAdapter = new ArrayAdapter<>(
                this, android.R.layout.simple_list_item_multiple_choice, new ArrayList<>());
        dialogList.setAdapter(dialogAdapter);
        root.addView(dialogList, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));

        saveButton = new Button(this);
        saveButton.setText(R.string.save);
        saveButton.setOnClickListener(view -> saveConfiguration());
        root.addView(saveButton, matchWrap());

        TextView hint = new TextView(this);
        hint.setText(R.string.refresh_hint);
        hint.setPadding(0, dp(8), 0, 0);
        root.addView(hint, matchWrap());
        return root;
    }

    private void registerCatalogUpdates() {
        if (catalogUpdatesRegistered) {
            return;
        }
        IntentFilter filter = new IntentFilter(CatalogProtocol.ACTION_UPDATED);
        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(catalogUpdatedReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            registerLegacyCatalogReceiver(filter);
        }
        catalogUpdatesRegistered = true;
    }

    @SuppressWarnings("deprecation")
    @SuppressLint("UnspecifiedRegisterReceiverFlag")
    private void registerLegacyCatalogReceiver(IntentFilter filter) {
        registerReceiver(catalogUpdatedReceiver, filter);
    }

    private void requestCatalog() {
        catalogRequestTimedOut = false;
        renderCached();
        mainHandler.removeCallbacks(catalogTimeout);
        try {
            requestExpiresAt = CatalogRequestClient.request(this);
            long delay = Math.max(0, requestExpiresAt - SystemClock.elapsedRealtime());
            mainHandler.postDelayed(catalogTimeout, delay + 100);
        } catch (RuntimeException error) {
            requestExpiresAt = 0;
            catalogRequestTimedOut = true;
            renderCached();
        }
    }

    private void renderCached() {
        HiddenConfig config = HiddenConfig.empty();
        preferencesAvailable = true;
        try {
            config = ConfigStore.load(this);
        } catch (SecurityException error) {
            preferencesAvailable = false;
        }

        catalog.clear();
        catalog.addAll(catalogRepository.loadCatalog());
        addMissingSelections(config.hiddenDialogs());
        java.util.Collections.sort(catalog);

        dialogAdapter.clear();
        for (CatalogEntry entry : catalog) {
            dialogAdapter.add(formatEntry(entry));
        }
        dialogAdapter.notifyDataSetChanged();
        dialogList.clearChoices();
        for (int index = 0; index < catalog.size(); index++) {
            dialogList.setItemChecked(index, config.isHidden(catalog.get(index).key()));
        }

        setSuppressNotifications(config.suppressNotifications());
        saveButton.setEnabled(preferencesAvailable);
        statusView.setText(formatStatus(catalogRepository.loadHookStatuses()));
    }

    private void addMissingSelections(Set<DialogKey> selected) {
        Set<DialogKey> present = new HashSet<>();
        for (CatalogEntry entry : catalog) {
            present.add(entry.key());
        }
        for (DialogKey key : selected) {
            if (!present.contains(key)) {
                catalog.add(new CatalogEntry(key, "Unavailable from current catalog"));
            }
        }
    }

    private void saveConfiguration() {
        Set<DialogKey> selected = new HashSet<>();
        for (int index = 0; index < catalog.size(); index++) {
            if (dialogList.isItemChecked(index)) {
                selected.add(catalog.get(index).key());
            }
        }

        try {
            boolean saved = ConfigStore.save(
                    this, selected, notificationSwitch.isChecked());
            if (!saved) {
                showError(getString(R.string.save_failed));
                return;
            }
            Toast.makeText(this, "Changes saved", Toast.LENGTH_SHORT).show();
        } catch (SecurityException error) {
            preferencesAvailable = false;
            saveButton.setEnabled(false);
            showError(getString(R.string.framework_inactive));
        }
    }

    private void setSuppressNotifications(boolean enabled) {
        notificationSwitch.setChecked(enabled);
    }

    private String formatEntry(CatalogEntry entry) {
        return entry.title()
                + "\nAccount " + (entry.key().account() + 1)
                + " • ID " + entry.key().dialogId();
    }

    private String formatStatus(Map<String, String> statuses) {
        StringBuilder text = new StringBuilder();
        if (!preferencesAvailable) {
            text.append(getString(R.string.framework_inactive)).append("\n\n");
        }
        text.append(getString(R.string.hook_status)).append('\n');
        if (statuses.isEmpty()) {
            text.append(getString(R.string.open_telegram_first));
        } else {
            for (Map.Entry<String, String> status : statuses.entrySet()) {
                text.append(status.getKey()).append(": ").append(status.getValue()).append('\n');
            }
        }
        if (catalogRequestTimedOut) {
            text.append('\n').append(getString(R.string.catalog_refresh_timeout));
        }
        return text.toString().trim();
    }

    private void showError(String message) {
        new AlertDialog.Builder(this)
                .setTitle(R.string.app_name)
                .setMessage(message)
                .setPositiveButton(android.R.string.ok, null)
                .show();
    }

    private LinearLayout.LayoutParams matchWrap() {
        return new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
