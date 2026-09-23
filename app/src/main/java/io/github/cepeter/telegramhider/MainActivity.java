package io.github.cepeter.telegramhider;

import android.app.Activity;
import android.app.AlertDialog;
import android.os.Bundle;
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
import io.github.cepeter.telegramhider.catalog.CatalogRepository;
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

    private CatalogRepository catalogRepository;
    private TextView statusView;
    private ListView dialogList;
    private ArrayAdapter<String> dialogAdapter;
    private Switch notificationSwitch;
    private Button saveButton;
    private boolean preferencesAvailable;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        catalogRepository = new CatalogRepository(this);
        setContentView(buildContentView());
    }

    @Override
    protected void onResume() {
        super.onResume();
        refresh();
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
        refreshButton.setOnClickListener(view -> refresh());
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

    private void refresh() {
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
