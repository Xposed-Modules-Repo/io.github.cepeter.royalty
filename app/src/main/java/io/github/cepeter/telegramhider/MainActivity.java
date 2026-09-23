package io.github.cepeter.telegramhider;

import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;

public final class MainActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        TextView message = new TextView(this);
        message.setPadding(48, 48, 48, 48);
        message.setText("Telegram Chat Hider 2.0 migration in progress");
        setContentView(message);
    }
}
