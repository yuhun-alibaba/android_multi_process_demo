package weex.alibaba.com.multiprocessdemo;

import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.widget.TextView;

public class MainActivity extends AppCompatActivity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        DemoManger.getInstance().loadlibrary();

        setContentView(R.layout.activity_main);
        TextView textView = (TextView) findViewById(R.id.hello);
        textView.setClickable(true);
        textView.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View arg0) {
                // Log.e("shiwentao", "textview onclick");
                // DemoManger.getInstance().callSoMethod();
                DemoManger.getInstance().initFramework();
            }

        });
    }
}
