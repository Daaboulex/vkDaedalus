#include <X11/Xlib.h>
#include <stdio.h>
#include <string.h>

static void answer_grab(Display *display, Window root)
{
    int result = XGrabPointer(display, root, False, 0, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);

    if (result == GrabSuccess)
    {
        XUngrabPointer(display, CurrentTime);
        XSync(display, False);
        printf("free\n");
        return;
    }

    printf("%s\n", result == AlreadyGrabbed ? "held" : "unavailable");
}

static void answer_focus(Display *display, Window root)
{
    XSetInputFocus(display, root, RevertToNone, CurrentTime);
    XSync(display, False);
    printf("focused\n");
}

int main(void)
{
    Display *display = XOpenDisplay(NULL);
    if (display == NULL)
    {
        printf("no-display\n");
        fflush(stdout);
        return 2;
    }

    Window root = DefaultRootWindow(display);
    char line[64];

    printf("ready\n");
    fflush(stdout);

    while (fgets(line, sizeof line, stdin) != NULL)
    {
        if (strncmp(line, "grab", 4) == 0)
            answer_grab(display, root);
        else if (strncmp(line, "focus", 5) == 0)
            answer_focus(display, root);
        else
            printf("unknown\n");

        fflush(stdout);
    }

    XCloseDisplay(display);
    return 0;
}
