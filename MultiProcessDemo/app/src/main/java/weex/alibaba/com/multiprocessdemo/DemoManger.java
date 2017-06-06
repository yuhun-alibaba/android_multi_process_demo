package weex.alibaba.com.multiprocessdemo;

import android.os.Handler;
import android.os.Message;
import android.util.Log;

/**
 * Created by shiwentao on 2017/6/1.
 */

public class DemoManger {
    static volatile DemoManger mDomManager;
    public final static String LIB = "weexjsc";
    public static final int LOAD_LIBRARY = 1;
    public static final int INIT_FRAMEWORK = 2;

    private Handler handler = new Handler() {
        public void handleMessage(Message msg) {
            switch (msg.what) {
                case LOAD_LIBRARY:
                    System.loadLibrary(LIB);
                    break;
                case INIT_FRAMEWORK:
                    callSoMethod();
                    break;
                default:
                    break;
            }
        }
    };
    public static DemoManger getInstance() {
        if (mDomManager == null) {
            synchronized (DemoManger.class) {
                if (mDomManager == null) {
                    mDomManager = new DemoManger();
                }
            }
        }
        return mDomManager;
    }


    public void loadlibrary() {
        new Thread(new Runnable() {
            @Override
            public void run() {
                Message message = new Message();
                message.what = LOAD_LIBRARY;
                handler.sendMessage(message);
            }
        }).start();

    }

    public void initFramework() {
        new Thread(new Runnable() {
            @Override
            public void run() {
                Message message = new Message();
                message.what = INIT_FRAMEWORK;
                handler.sendMessage(message);
            }
        }).start();

    }

    public void callSoMethod() {
        Log.e("shiwentao", "callSoMethod initFramework");
        initFramework("initFramework");
    }

    public native int initFramework(String framework);
    public native int execJS(String framework);

    /**
     * JavaScript uses this methods to call Android code
     *
     * @param instanceId
     */

    public int callNative(String instanceId) {
        return 0;
    }

    /**
     * JavaScript uses this methods to call Android code
     *
     * @param instanceId
     */

    public int callNativeLog(String instanceId) {
        return 0;
    }

}
