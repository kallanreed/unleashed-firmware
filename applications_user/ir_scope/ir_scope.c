#include <furi.h>
#include <furi_hal.h>
#include <infrared.h>
#include <infrared_worker.h>
#include <furi_hal_infrared.h>

#include <gui/gui.h>

#define TAG "IR Scope"
#define WIDTH 128
#define SAMPLES_CNT 1024
#define ARRAY_LEN 128

typedef struct
{
    uint8_t samples[ARRAY_LEN];
    FuriMutex* mutex;
} IRScopeState;

bool ir_scope_state_get_sample(const IRScopeState* state, size_t sample_ix)
{
    furi_check(sample_ix < SAMPLES_CNT);

    size_t ix = sample_ix / 8;
    furi_check(ix < ARRAY_LEN);
    const uint8_t* sample = &state->samples[ix];

    switch (sample_ix % 8)
    {
        case 0: return (*sample & 0x01) == 0x01;
        case 1: return (*sample & 0x02) == 0x02;
        case 2: return (*sample & 0x04) == 0x04;
        case 3: return (*sample & 0x08) == 0x08;
        case 4: return (*sample & 0x10) == 0x10;
        case 5: return (*sample & 0x20) == 0x20;
        case 6: return (*sample & 0x40) == 0x40;
        case 7: return (*sample & 0x80) == 0x80;
        default:
            furi_assert(false);
            return false;
    }
}

void ir_scope_state_set_sample(IRScopeState* state, size_t sample_ix, bool val)
{
    furi_check(sample_ix < SAMPLES_CNT);

    size_t ix = sample_ix / 8;
    furi_check(ix < ARRAY_LEN);
    uint8_t* sample = &state->samples[ix];
    
    switch (sample_ix % 8)
    {
        case 0: *sample = val ? (*sample | 0x01 ) : (*sample & ~0x01); break;
        case 1: *sample = val ? (*sample | 0x02 ) : (*sample & ~0x02); break;
        case 2: *sample = val ? (*sample | 0x04 ) : (*sample & ~0x04); break;
        case 3: *sample = val ? (*sample | 0x08 ) : (*sample & ~0x08); break;
        case 4: *sample = val ? (*sample | 0x10 ) : (*sample & ~0x10); break;
        case 5: *sample = val ? (*sample | 0x20 ) : (*sample & ~0x20); break;
        case 6: *sample = val ? (*sample | 0x40 ) : (*sample & ~0x40); break;
        case 7: *sample = val ? (*sample | 0x80 ) : (*sample & ~0x80); break;
    }
}

static void render_callback(Canvas* canvas, void* ctx)
{
    const IRScopeState* state = (IRScopeState*)ctx;

    furi_mutex_acquire(state->mutex, FuriWaitForever);

    canvas_clear(canvas);
    canvas_draw_frame(canvas, 0, 0, 128, 64);

    size_t rows = SAMPLES_CNT / WIDTH;

    for (size_t row = 0; row < rows; ++row)
    {
        for (size_t col = 0; col < WIDTH; ++col)
        {
            size_t ix = row * WIDTH + col;
            int height = ir_scope_state_get_sample(state, ix) ? 5 : 0;
            int y = row * 8 + 7;
            canvas_draw_line(canvas, col, y, col, y - height);
        }
    }

    furi_mutex_release(state->mutex);
}

static void input_callback(InputEvent* input_event, void* ctx)
{
    FuriMessageQueue* event_queue = ctx;
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

static void ir_received_callback(void* ctx, InfraredWorkerSignal* signal)
{
    furi_check(signal);
    IRScopeState* state = (IRScopeState*)ctx;

    const uint32_t* timings;
    size_t timings_cnt;
    uint32_t length = 0;

    infrared_worker_get_raw_signal(signal, &timings, &timings_cnt);

    for (size_t i = 0; i < timings_cnt; ++i)
        length += timings[i];

    // Timings are in microseconds and alternate On/Off.
    uint32_t us_per_sample = length / SAMPLES_CNT;

    furi_mutex_acquire(state->mutex, FuriWaitForever);

    bool high = true;
    size_t ix = 0;
    for (size_t i = 0; i < timings_cnt; ++i)
    {
        for (size_t j = 0; j < timings[i] / us_per_sample && ix < SAMPLES_CNT; ++j)
        {
            ir_scope_state_set_sample(state, ix, high);
            ++ix;
        }

        high = !high;
    }

    furi_mutex_release(state->mutex);
}

int32_t ir_scope_app(void* p)
{
    UNUSED(p);

    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    furi_check(event_queue);

    if(furi_hal_infrared_is_busy())
    {
        FURI_LOG_E(TAG, "Infrared is busy.");
        return -1;    
    }

    IRScopeState state = { .mutex = NULL };
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

    InfraredWorker* worker = infrared_worker_alloc();
    infrared_worker_rx_enable_signal_decoding(worker, false);
    infrared_worker_rx_enable_blink_on_receiving(worker, true);
    infrared_worker_rx_set_received_signal_callback(worker, ir_received_callback, &state);
    infrared_worker_rx_start(worker);

    InputEvent event;
    bool processing = true;
    while(processing && furi_message_queue_get(event_queue, &event, FuriWaitForever) == FuriStatusOk)
    {
        if (event.type == InputTypeRelease && event.key == InputKeyBack)
            processing = false;
    }

    // Clean up.
    infrared_worker_rx_stop(worker);
    infrared_worker_free(worker);

    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    furi_record_close("gui");
    view_port_free(view_port);
    furi_message_queue_free(event_queue);
    furi_mutex_free(state.mutex);

    return 0;
}
