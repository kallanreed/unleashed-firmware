#include <furi.h>
#include <furi_hal.h>

#include <gui/gui.h>

#define TAG "IR Scpe"

typedef struct
{
  FuriMutex* mutex;
} IRScopeState;

static void render_callback(Canvas* canvas, void* ctx)
{
    const IRScopeState* state = (IRScopeState*)ctx;

    furi_mutex_acquire(state->mutex, FuriWaitForever);

    canvas_draw_frame(canvas, 0, 0, 128, 64);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(
        canvas, state->x, state->y, AlignRight, AlignBottom, "Hello World!");

    furi_mutex_release(state->mutex);
}

static void input_callback(InputEvent* input_event, void* ctx)
{
    FuriMessageQueue* event_queue = ctx;
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

int32_t ir_scope_app(void* p)
{
    UNUSED(p);
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    furi_check(event_queue);

    IRScopeState state = {50, 50, NULL};
    state.mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    if(!state.mutex)
    {
        FURI_LOG_E(TAG, "Cannot create mutex.");
        return -1;
    }

    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, render_callback, &state);
    view_port_input_callback_set(view_port, input_callback, event_queue);

    Gui* gui = furi_record_open("gui");
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    InputEvent event;
    bool processing = true;
    while(processing && furi_message_queue_get(event_queue, &event, FuriWaitForever) == FuriStatusOk)
    {
        furi_mutex_acquire(state.mutex, FuriWaitForever);

        if (event.type == InputTypeRelease)
        {
            switch(event.key)
            {
            case InputKeyUp:
                state.y--;
                break;
            case InputKeyDown:
                state.y++;
                break;
            case InputKeyRight:
                state.x++;
                break;
            case InputKeyLeft:
                state.x--;
                break;
            case InputKeyOk:
            case InputKeyBack:
                processing = false;
                break;

            default:
                break;
            }
        }

        view_port_update(view_port);
        furi_mutex_release(state.mutex);
    }

    // Clean up.
    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    furi_record_close("gui");
    view_port_free(view_port);
    furi_message_queue_free(event_queue);
    furi_mutex_free(state.mutex);

    return 0;
}
