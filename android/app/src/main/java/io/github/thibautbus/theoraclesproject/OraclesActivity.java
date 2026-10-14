package io.github.thibautbus.theoraclesproject;

import android.database.Cursor;
import android.graphics.Insets;
import android.graphics.Rect;
import android.net.Uri;
import android.os.Build;
import android.provider.OpenableColumns;
import android.app.Activity;
import android.view.DisplayCutout;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.RoundedCorner;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;

import org.libsdl.app.SDLActivity;

/* The launcher's activity: SDL's, which loads libSDL3.so then libmain.so and runs main on its own thread. */
public class OraclesActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }

    /* Back from a keyboard that has arrows (the emulator's, many a hardware keyboard: Android gives them the DPAD
     * source) is the launcher's Back, as the system's is: SDL would take any DPAD device for a controller and make its
     * Back a controller's "back" button, the game's Select.  A real controller's Back stays SDL's, a game button. */
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getAction() == KeyEvent.ACTION_DOWN) {
            if (controller(event.getDevice())) { controllerKeys++; lastKeyDevice = event.getDevice().getName(); }
            else otherKeys++;
        }
        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK && !controller(event.getDevice())) {
            if (event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0) SDLActivity.onNativeKeyDown(KeyEvent.KEYCODE_BACK);
            else if (event.getAction() == KeyEvent.ACTION_UP) SDLActivity.onNativeKeyUp(KeyEvent.KEYCODE_BACK);
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if ((event.getSource() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK) controllerMotions++;
        return super.dispatchGenericMotionEvent(event);
    }

    /* What reached the activity since the last call, for the launcher's report after a game: the keys pressed on a
     * controller and elsewhere, the controllers' motions, the view that has the focus and the window's focus. */
    private static volatile int controllerKeys, otherKeys, controllerMotions;
    private static volatile String lastKeyDevice = "";

    public static String inputState() {
        final Activity activity = (Activity) getContext();
        final View focus = activity != null ? activity.getCurrentFocus() : null;
        final String state = "controller keys " + controllerKeys + " (" + lastKeyDevice + "), other keys " + otherKeys
            + ", controller motions " + controllerMotions + ", focused view " + (focus != null ? focus.getClass().getSimpleName() : "none")
            + ", window focus " + (activity != null && activity.hasWindowFocus()) + ", display "
            + (activity != null ? activity.getWindowManager().getDefaultDisplay().getDisplayId() : -1);
        controllerKeys = otherKeys = controllerMotions = 0;
        return state;
    }

    /* What the system takes of the window, for the launcher's log beside SDL's safe area (launcher/safe_area.c): the
     * window's bounds and the largest it could have, the insets of each kind (left, top, right, bottom) and those of the
     * bars were they shown, whether the bars show, the cutout's rectangles, the rounded corners' radii, the cutout mode
     * and the system UI flags SDL's fullscreen set. */
    public static String safeArea() {
        final Activity activity = (Activity) getContext();
        if (activity == null) return "";
        final Window window = activity.getWindow();
        final View decor = window != null ? window.getDecorView() : null;
        if (decor == null) return "";
        final StringBuilder s = new StringBuilder("android: view ").append(decor.getWidth()).append('x').append(decor.getHeight());
        if (Build.VERSION.SDK_INT >= 30) {
            s.append(", window ").append(rect(activity.getWindowManager().getCurrentWindowMetrics().getBounds()))
             .append(" of ").append(rect(activity.getWindowManager().getMaximumWindowMetrics().getBounds()));
        }
        s.append(", display ").append(activity.getWindowManager().getDefaultDisplay().getDisplayId());
        final WindowInsets insets = decor.getRootWindowInsets();
        if (insets != null && Build.VERSION.SDK_INT >= 30) {
            s.append(", cutout ").append(insets(insets.getInsets(WindowInsets.Type.displayCutout())))
             .append(", status bar ").append(insets(insets.getInsets(WindowInsets.Type.statusBars())))
             .append(insets.isVisible(WindowInsets.Type.statusBars()) ? " shown" : " hidden")
             .append(", navigation bar ").append(insets(insets.getInsets(WindowInsets.Type.navigationBars())))
             .append(insets.isVisible(WindowInsets.Type.navigationBars()) ? " shown" : " hidden")
             .append(", bars when shown ").append(insets(insets.getInsetsIgnoringVisibility(WindowInsets.Type.systemBars())))
             .append(", gestures ").append(insets(insets.getInsets(WindowInsets.Type.systemGestures())))
             .append(", mandatory gestures ").append(insets(insets.getInsets(WindowInsets.Type.mandatorySystemGestures())))
             .append(", tappable ").append(insets(insets.getInsets(WindowInsets.Type.tappableElement())));
        } else if (insets != null) {
            s.append(", system window ").append(insets.getSystemWindowInsetLeft()).append(',').append(insets.getSystemWindowInsetTop())
             .append(',').append(insets.getSystemWindowInsetRight()).append(',').append(insets.getSystemWindowInsetBottom());
        }
        final DisplayCutout cutout = insets != null ? insets.getDisplayCutout() : null;
        if (cutout != null) {
            s.append(", cutout rectangles");
            for (Rect r : cutout.getBoundingRects()) s.append(' ').append(rect(r));
            if (Build.VERSION.SDK_INT >= 30) s.append(", waterfall ").append(insets(cutout.getWaterfallInsets()));
        }
        if (insets != null && Build.VERSION.SDK_INT >= 31) {
            s.append(", corners");
            for (int position = RoundedCorner.POSITION_TOP_LEFT; position <= RoundedCorner.POSITION_BOTTOM_LEFT; position++) {
                final RoundedCorner corner = insets.getRoundedCorner(position);
                s.append(' ').append(corner != null ? corner.getRadius() : 0);
            }
        }
        s.append(", cutout mode ").append(window.getAttributes().layoutInDisplayCutoutMode)
         .append(", UI flags 0x").append(Integer.toHexString(decor.getSystemUiVisibility()));
        return s.toString();
    }

    /* The display cutout's safe insets at the window's top and bottom, "top bottom" in pixels, a curved edge's included:
     * the band the launcher keeps the game's frame, its controls and its pages in (launcher/safe_area.c). */
    public static String cutoutInsets() {
        final Activity activity = (Activity) getContext();
        final Window window = activity != null ? activity.getWindow() : null;
        final WindowInsets insets = window != null ? window.getDecorView().getRootWindowInsets() : null;
        final DisplayCutout cutout = insets != null ? insets.getDisplayCutout() : null;
        return cutout != null ? cutout.getSafeInsetTop() + " " + cutout.getSafeInsetBottom() : "0 0";
    }

    /* The display's pixels per dp (its DisplayMetrics' density): the size of the screen margin's small and large
     * margins (launcher/safe_area.c). */
    public static String displayDensity() {
        final Activity activity = (Activity) getContext();
        return activity != null ? Float.toString(activity.getResources().getDisplayMetrics().density) : "1";
    }

    private static String rect(Rect r) {
        return r.left + "," + r.top + " " + r.width() + "x" + r.height();
    }

    private static String insets(Insets i) {
        return i.left + "," + i.top + "," + i.right + "," + i.bottom;
    }

    private static boolean controller(InputDevice device) {
        if (device == null) return false;
        final int sources = device.getSources();
        return (sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
            || (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
    }

    /* The name the document picker shows for a content:// URI ("Oracle of Ages.gbc"), or null: the launcher copies a
     * chosen ROM or patch under that name (launcher/file_dialog.c). */
    public static String displayName(String uri) {
        try (Cursor cursor = getContext().getContentResolver().query(Uri.parse(uri),
                new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) return cursor.getString(0);
        } catch (Exception ignored) {
        }
        return null;
    }
}
