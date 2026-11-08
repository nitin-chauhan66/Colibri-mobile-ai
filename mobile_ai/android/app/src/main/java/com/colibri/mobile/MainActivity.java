package com.colibri.mobile;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

public final class MainActivity extends Activity {
    private TextView output;
    private long engine;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        engine = NativeEngine.create(getFilesDir().getAbsolutePath());

        output = new TextView(this);
        output.setText("Colibri Mobile AI\n\nOffline mmap inference prototype ready.");
        output.setTextSize(18);
        output.setPadding(32, 48, 32, 24);

        Button run = new Button(this);
        run.setText("Run smoke inference");
        run.setOnClickListener(view -> runSmokeInference());

        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.addView(output);
        content.addView(run);
        setContentView(new ScrollView(this) {{
            addView(content);
        }});
    }

    private void runSmokeInference() {
        output.append("\n\nRunning native engine...");
        final int[] tokens = {1, 2, 3, 4};
        new Thread(() -> {
            String result = NativeEngine.run(engine, tokens);
            new Handler(Looper.getMainLooper()).post(() -> output.append("\n" + result));
        }).start();
    }

    @Override
    protected void onDestroy() {
        NativeEngine.destroy(engine);
        engine = 0;
        super.onDestroy();
    }
}
