#include "radio_stream.h"

#include <furi.h>
#include <string.h>

#include <storage/storage.h>
#include <network/network.h>

#include <lwip/api.h>
#include <lwip/sockets.h>
#include <lwip/err.h>
#include <lwip/ip_addr.h>

#include <mp3_decoder/mp3_decoder.h>
#include <pcm_output/pcm_output.h>

#define TAG "RadioStream"

#define RS_NET_STACK_SIZE  (1024 * 48)
#define RS_FILE_STACK_SIZE (1024 * 32)
#define RS_RECV_BUF_SIZE   4096
#define RS_HEADER_MAX      8192
#define RS_INACTIVITY_MS   30000
#define RS_MAX_REDIRECTS   3

#define RS_ICY_META_MAX    512
#define RS_ICY_TITLE_SIZE  256
#define RS_ICY_NAME_SIZE   256

struct RadioStream {
    Mp3Decoder* decoder;
    PcmOutput* output;
    int16_t* decode_buf;
    float volume;
    _Atomic bool playing;
    _Atomic bool stop_requested;
    _Atomic uint32_t last_data_tick;
    _Atomic uint32_t last_audio_tick;
    bool pcm_started;

    FuriThread* net_thread;
    FuriString* url;

    Network* network;

    /* ICY metadata */
    FuriMutex* icy_mutex;
    uint32_t icy_metaint;
    uint32_t icy_counter;
    bool icy_reading_meta;
    uint32_t icy_meta_size;
    uint32_t icy_meta_pos;
    uint8_t icy_meta_buf[RS_ICY_META_MAX];
    char icy_title[RS_ICY_TITLE_SIZE];
    char icy_name[RS_ICY_NAME_SIZE];
};

static bool radio_resolve_hostname(const char* host, ip_addr_t* address) {
    return netconn_gethostbyname(host, address) == ERR_OK;
}

static void radio_decode_and_output(RadioStream* rs, bool drain) {
    while(true) {
        size_t out_free = pcm_output_free_space(rs->output);
        if(out_free < 1152) break;

        size_t cap = (out_free < MP3_DECODER_MAX_OUTPUT_SAMPLES)
            ? out_free : MP3_DECODER_MAX_OUTPUT_SAMPLES;
        size_t samples = drain ?
            mp3_decoder_drain(rs->decoder, rs->decode_buf, cap) :
            mp3_decoder_decode(rs->decoder, rs->decode_buf, cap);
        if(samples == 0) break;

        if(rs->volume < 1.0f) {
            for(size_t i = 0; i < samples; i++) {
                rs->decode_buf[i] = (int16_t)(rs->decode_buf[i] * rs->volume);
            }
        }

        if(pcm_output_write(rs->output, rs->decode_buf, samples) > 0) {
            rs->last_audio_tick = furi_get_tick();
        }
    }
}

static void radio_on_data(uint8_t* data, size_t data_size, void* context) {
    RadioStream* rs = context;

    size_t offset = 0;
    while(offset < data_size && !rs->stop_requested) {
        size_t space = mp3_decoder_space(rs->decoder);
        if(space == 0) {
            radio_decode_and_output(rs, false);
            space = mp3_decoder_space(rs->decoder);
            if(space == 0) {
                furi_delay_ms(5);
                continue;
            }
        }

        size_t chunk = data_size - offset;
        if(chunk > space) chunk = space;

        mp3_decoder_feed(rs->decoder, data + offset, chunk);
        offset += chunk;

        radio_decode_and_output(rs, false);
    }
}

/**
 * Parse http://host:port/path from URL string.
 * Returns false if URL is malformed. host_out must be freed by caller.
 */
static bool radio_parse_url(
    const char* url,
    char** host_out,
    uint16_t* port_out,
    const char** path_out) {
    if(!url) return false;

    const char* p = url;
    if(strncmp(p, "http://", 7) == 0) {
        p += 7;
    }

    const char* host_start = p;
    const char* colon = NULL;
    const char* slash = NULL;

    while(*p && *p != '/' && *p != ':') p++;
    size_t host_len = (size_t)(p - host_start);
    if(host_len == 0) return false;

    if(*p == ':') {
        colon = p;
        p++;
        while(*p && *p != '/') p++;
    }
    slash = (*p == '/') ? p : NULL;

    *host_out = malloc(host_len + 1);
    if(!*host_out) return false;
    memcpy(*host_out, host_start, host_len);
    (*host_out)[host_len] = '\0';

    if(colon) {
        *port_out = (uint16_t)atoi(colon + 1);
    } else {
        *port_out = 80;
    }
    if(*port_out == 0) *port_out = 80;

    *path_out = slash ? slash : "/";
    return true;
}

static int radio_parse_http_status(const uint8_t* data, size_t header_size) {
    if(header_size < 8) return 0;

    const char* line = (const char*)data;
    const char* space = memchr(line, ' ', header_size);
    if(!space || (size_t)(space - line + 4) > header_size) return 0;

    if(strncmp(line, "HTTP/1.", 7) != 0 && strncmp(line, "ICY", 3) != 0) return 0;
    return atoi(space + 1);
}

static bool radio_parse_header_value(
    const uint8_t* data,
    size_t header_size,
    const char* name,
    char* value,
    size_t value_size) {
    const size_t name_len = strlen(name);
    for(size_t i = 0; i + name_len < header_size; i++) {
        if(i > 0 && data[i - 1] != '\n') continue;

        bool match = true;
        for(size_t j = 0; j < name_len; j++) {
            char c = (char)data[i + j];
            if(c >= 'A' && c <= 'Z') c += 32;
            char expected = name[j];
            if(expected >= 'A' && expected <= 'Z') expected += 32;
            if(c != expected) {
                match = false;
                break;
            }
        }
        if(!match || data[i + name_len] != ':') continue;

        size_t start = i + name_len + 1;
        while(start < header_size && (data[start] == ' ' || data[start] == '\t')) start++;
        size_t end = start;
        while(end < header_size && data[end] != '\r' && data[end] != '\n') end++;
        while(end > start && (data[end - 1] == ' ' || data[end - 1] == '\t')) end--;

        const size_t length = MIN(end - start, value_size - 1);
        memcpy(value, data + start, length);
        value[length] = '\0';
        return length > 0;
    }
    return false;
}

static bool radio_send_all(RadioStream* rs, int sock, const char* data, size_t size) {
    size_t sent_total = 0;
    while(sent_total < size && !rs->stop_requested) {
        const int sent = send(sock, data + sent_total, size - sent_total, 0);
        if(sent > 0) {
            sent_total += (size_t)sent;
            continue;
        }
        if(sent < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
            furi_delay_ms(5);
            continue;
        }
        return false;
    }
    return sent_total == size;
}

/**
 * Case-insensitive search for "icy-metaint:" in HTTP response headers.
 * Returns the metaint value or 0 if not found.
 */
static uint32_t radio_parse_icy_metaint(const uint8_t* data, size_t header_end) {
    const char* needle = "icy-metaint:";
    const size_t needle_len = 12;

    for(size_t i = 0; i + needle_len <= header_end; i++) {
        bool match = true;
        for(size_t j = 0; j < needle_len; j++) {
            char c = (char)data[i + j];
            if(c >= 'A' && c <= 'Z') c += 32;
            if(c != needle[j]) {
                match = false;
                break;
            }
        }
        if(match) {
            size_t k = i + needle_len;
            while(k < header_end && data[k] == ' ') k++;
            uint32_t val = 0;
            while(k < header_end && data[k] >= '0' && data[k] <= '9') {
                val = val * 10 + (data[k] - '0');
                k++;
            }
            return val;
        }
    }
    return 0;
}

/**
 * Extract icy-name: value from HTTP response headers (Icecast).
 */
static void radio_parse_icy_name(RadioStream* rs, const uint8_t* data, size_t header_end) {
    const char* needle = "icy-name:";
    const size_t needle_len = 9;

    for(size_t i = 0; i + needle_len <= header_end; i++) {
        bool match = true;
        for(size_t j = 0; j < needle_len; j++) {
            char c = (char)data[i + j];
            if(c >= 'A' && c <= 'Z') c += 32;
            if(c != needle[j]) {
                match = false;
                break;
            }
        }
        if(!match) continue;

        size_t k = i + needle_len;
        while(k < header_end && (data[k] == ' ' || data[k] == '\t')) k++;

        size_t start = k;
        while(k < header_end && data[k] != '\r' && data[k] != '\n') k++;
        while(k > start && (data[k - 1] == ' ' || data[k - 1] == '\t')) k--;

        size_t len = k - start;
        if(len >= RS_ICY_NAME_SIZE) len = RS_ICY_NAME_SIZE - 1;

        furi_mutex_acquire(rs->icy_mutex, FuriWaitForever);
        memcpy(rs->icy_name, data + start, len);
        rs->icy_name[len] = '\0';
        furi_mutex_release(rs->icy_mutex);

        FURI_LOG_I(TAG, "ICY name: %s", rs->icy_name);
        return;
    }
}

static void radio_parse_icy_title(RadioStream* rs, const char* meta) {
    const char* key = "StreamTitle='";
    const size_t key_len = 13;
    const char* p = strstr(meta, key);
    if(!p) return;

    p += key_len;
    const char* end = strstr(p, "';");
    if(!end) end = strchr(p, '\'');
    if(!end) return;

    size_t len = (size_t)(end - p);
    if(len >= RS_ICY_TITLE_SIZE) len = RS_ICY_TITLE_SIZE - 1;

    furi_mutex_acquire(rs->icy_mutex, FuriWaitForever);
    memcpy(rs->icy_title, p, len);
    rs->icy_title[len] = '\0';
    furi_mutex_release(rs->icy_mutex);

    FURI_LOG_I(TAG, "ICY title: %s", rs->icy_title);
}

/**
 * Process raw stream data with ICY metadata interleaved.
 * Strips metadata blocks and passes only audio to radio_on_data.
 */
static void radio_on_icy_stream(RadioStream* rs, uint8_t* data, size_t data_len) {
    if(rs->icy_metaint == 0) {
        radio_on_data(data, data_len, rs);
        return;
    }

    size_t pos = 0;
    while(pos < data_len && !rs->stop_requested) {
        if(rs->icy_reading_meta) {
            uint32_t need = rs->icy_meta_size - rs->icy_meta_pos;
            size_t avail = data_len - pos;
            uint32_t take = ((uint32_t)avail < need) ? (uint32_t)avail : need;

            if(rs->icy_meta_pos < RS_ICY_META_MAX) {
                uint32_t buf_avail = RS_ICY_META_MAX - rs->icy_meta_pos;
                uint32_t to_store = (take < buf_avail) ? take : buf_avail;
                memcpy(rs->icy_meta_buf + rs->icy_meta_pos, data + pos, to_store);
            }

            rs->icy_meta_pos += take;
            pos += take;

            if(rs->icy_meta_pos >= rs->icy_meta_size) {
                uint32_t safe = (rs->icy_meta_size < RS_ICY_META_MAX)
                    ? rs->icy_meta_size : (RS_ICY_META_MAX - 1);
                rs->icy_meta_buf[safe] = '\0';
                radio_parse_icy_title(rs, (const char*)rs->icy_meta_buf);

                rs->icy_reading_meta = false;
                rs->icy_counter = rs->icy_metaint;
            }
        } else if(rs->icy_counter == 0) {
            uint8_t meta_len = data[pos];
            pos++;

            if(meta_len == 0) {
                rs->icy_counter = rs->icy_metaint;
            } else {
                rs->icy_meta_size = (uint32_t)meta_len * 16;
                rs->icy_meta_pos = 0;
                rs->icy_reading_meta = true;
            }
        } else {
            size_t audio_bytes = data_len - pos;
            if(audio_bytes > rs->icy_counter) {
                audio_bytes = rs->icy_counter;
            }

            radio_on_data(data + pos, audio_bytes, rs);
            pos += audio_bytes;
            rs->icy_counter -= (uint32_t)audio_bytes;
        }
    }
}

static int32_t radio_net_thread(void* context) {
    RadioStream* rs = context;
    int sock = -1;
    char* host = NULL;
    uint16_t port = 80;
    const char* path = "/";
    uint8_t* recv_buf = NULL;
    uint8_t* header_buf = NULL;
    uint8_t redirect_count = 0;

    FURI_LOG_I(TAG, "Net: starting, url=%s", furi_string_get_cstr(rs->url));

    if(!radio_parse_url(furi_string_get_cstr(rs->url), &host, &port, &path)) {
        FURI_LOG_E(TAG, "Net: bad URL");
        goto cleanup_nonet;
    }
    FURI_LOG_I(TAG, "Net: host=%s port=%u path=%s", host, port, path);

    recv_buf = malloc(RS_RECV_BUF_SIZE);
    header_buf = malloc(RS_HEADER_MAX);
    if(!recv_buf || !header_buf) {
        FURI_LOG_E(TAG, "Net: buffer allocation failed");
        goto cleanup_nonet;
    }

    rs->network = furi_record_open(RECORD_NETWORK);
    network_init_current_thread(rs->network);
    FURI_LOG_I(TAG, "Net: network initialized");

    if(rs->stop_requested) {
        FURI_LOG_W(TAG, "Net: stop_requested before socket");
        goto cleanup;
    }

connect_url:
    ip_addr_t resolved;
    if(!radio_resolve_hostname(host, &resolved)) {
        FURI_LOG_E(TAG, "Net: DNS failed for '%s'", host);
        goto cleanup;
    }
    FURI_LOG_I(TAG, "Net: resolved %s -> %s", host, ipaddr_ntoa(&resolved));

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(sock < 0) {
        FURI_LOG_E(TAG, "Net: socket() failed");
        goto cleanup;
    }
    FURI_LOG_I(TAG, "Net: socket=%d", sock);

    lwip_fcntl(sock, F_SETFL, O_NONBLOCK);

    {
        struct sockaddr_in sa;
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(port);
        sa.sin_addr.s_addr = ip_addr_get_ip4_u32(&resolved);

        FURI_LOG_I(TAG, "Net: connect(%s:%u)...", host, port);
        int cr = connect(sock, (struct sockaddr*)&sa, sizeof(sa));
        FURI_LOG_I(TAG, "Net: connect()=%d errno=%d", cr, errno);
    }

    /* Wait for connect completion via select(writable) */
    {
        fd_set wset;
        struct timeval tv;
        bool connected = false;
        for(int i = 0; i < 100 && !rs->stop_requested; i++) {
            FD_ZERO(&wset);
            FD_SET(sock, &wset);
            tv.tv_sec = 0;
            tv.tv_usec = 100000;
            int sel = select(sock + 1, NULL, &wset, NULL, &tv);
            if(sel > 0 && FD_ISSET(sock, &wset)) {
                int err = 0;
                socklen_t elen = sizeof(err);
                getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &elen);
                if(err == 0) {
                    connected = true;
                    break;
                }
                FURI_LOG_E(TAG, "Net: connect SO_ERROR=%d", err);
                goto close_sock;
            }
        }
        if(!connected) {
            FURI_LOG_E(TAG, "Net: connect timeout (10s)");
            goto close_sock;
        }
    }
    FURI_LOG_I(TAG, "Net: connected");

    /* Build and send HTTP GET */
    {
        FuriString* req = furi_string_alloc_printf(
            "GET %s HTTP/1.0\r\n"
            "Host: %s:%u\r\n"
            "User-Agent: BusyBar/1.0\r\n"
            "Icy-MetaData: 1\r\n"
            "Accept: */*\r\n\r\n",
            path, host, port);
        const char* req_str = furi_string_get_cstr(req);
        const size_t request_size = strlen(req_str);
        const bool sent = radio_send_all(rs, sock, req_str, request_size);
        FURI_LOG_I(TAG, "Net: sent GET (%zu bytes, ok=%d)", request_size, sent);
        furi_string_free(req);
        if(!sent) {
            FURI_LOG_E(TAG, "Net: send failed");
            goto close_sock;
        }
    }

    /* Non-blocking recv loop */
    {
        bool headers_done = false;
        uint32_t total_recv = 0;
        uint32_t total_sel_timeouts = 0;
        uint32_t last_data_tick = furi_get_tick();
        uint32_t last_stats_tick = last_data_tick;
        size_t header_size = 0;

        FURI_LOG_I(TAG, "Net: entering recv loop");
        while(!rs->stop_requested) {
            uint32_t now = furi_get_tick();
            if(now - last_data_tick > RS_INACTIVITY_MS) {
                FURI_LOG_W(TAG, "Net: inactivity timeout (%lu ms)", (unsigned long)RS_INACTIVITY_MS);
                break;
            }

            fd_set rset;
            FD_ZERO(&rset);
            FD_SET(sock, &rset);
            struct timeval stv = {.tv_sec = 0, .tv_usec = 50000};
            int sel = select(sock + 1, &rset, NULL, NULL, &stv);

            if(sel < 0) {
                FURI_LOG_E(TAG, "Net: select error=%d", errno);
                break;
            }
            if(sel == 0) {
                total_sel_timeouts++;
                if(headers_done) radio_decode_and_output(rs, false);
                furi_thread_yield();
                continue;
            }

            int n = recv(sock, recv_buf, RS_RECV_BUF_SIZE, 0);
            if(n <= 0) {
                if(n < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
                    furi_thread_yield();
                    continue;
                }
                FURI_LOG_I(TAG, "Net: connection closed (n=%d errno=%d)", n, errno);
                break;
            }

            last_data_tick = furi_get_tick();
            rs->last_data_tick = last_data_tick;
            total_recv += (uint32_t)n;

            /* Periodic stats every 5 seconds */
            if(last_data_tick - last_stats_tick > 5000) {
                FURI_LOG_I(
                    TAG,
                    "Net: total=%luKB dec=%zu pcm=%zu buf=%u und=%lu",
                    (unsigned long)(total_recv / 1024),
                    mp3_decoder_buffered(rs->decoder),
                    pcm_output_available(rs->output),
                    pcm_output_is_buffering(rs->output),
                    (unsigned long)pcm_output_underrun_count(rs->output));
                last_stats_tick = last_data_tick;
            }

            uint8_t* data = recv_buf;
            size_t data_len = (size_t)n;

            if(!headers_done) {
                if(data_len > RS_HEADER_MAX - header_size) {
                    FURI_LOG_E(TAG, "Net: HTTP headers exceed %u bytes", RS_HEADER_MAX);
                    goto close_sock;
                }

                memcpy(header_buf + header_size, data, data_len);
                header_size += data_len;

                for(size_t i = 0; i + 3 < header_size; i++) {
                    if(header_buf[i] == '\r' && header_buf[i + 1] == '\n' &&
                       header_buf[i + 2] == '\r' && header_buf[i + 3] == '\n') {
                        headers_done = true;
                        FURI_LOG_I(
                            TAG,
                            "Net: headers done (hdr=%zu, body=%zu)",
                            i + 4,
                            header_size - (i + 4));

                        const int status = radio_parse_http_status(header_buf, i);
                        if(status >= 300 && status < 400) {
                            char location[512];
                            if(redirect_count >= RS_MAX_REDIRECTS ||
                               !radio_parse_header_value(
                                   header_buf,
                                   i,
                                   "Location",
                                   location,
                                   sizeof(location))) {
                                FURI_LOG_E(TAG, "Net: redirect without usable Location");
                                goto close_sock;
                            }

                            redirect_count++;
                            if(strncmp(location, "http://", 7) == 0) {
                                furi_string_set(rs->url, location);
                            } else if(location[0] == '/') {
                                furi_string_printf(
                                    rs->url, "http://%s:%u%s", host, port, location);
                            } else {
                                FURI_LOG_E(TAG, "Net: unsupported relative redirect");
                                goto close_sock;
                            }

                            FURI_LOG_I(
                                TAG,
                                "Net: following redirect %u -> %s",
                                redirect_count,
                                furi_string_get_cstr(rs->url));
                            close(sock);
                            sock = -1;
                            free(host);
                            host = NULL;
                            if(!radio_parse_url(
                                   furi_string_get_cstr(rs->url),
                                   &host,
                                   &port,
                                   &path)) {
                                FURI_LOG_E(TAG, "Net: invalid redirect URL");
                                goto cleanup;
                            }
                            goto connect_url;
                        }

                        if(status < 200 || status >= 300) {
                            FURI_LOG_E(TAG, "Net: HTTP status %d", status);
                            goto close_sock;
                        }

                        rs->icy_metaint = radio_parse_icy_metaint(header_buf, i);
                        rs->icy_counter = rs->icy_metaint;
                        rs->icy_reading_meta = false;
                        rs->icy_meta_pos = 0;
                        rs->icy_meta_size = 0;
                        radio_parse_icy_name(rs, header_buf, i);
                        if(rs->icy_metaint > 0) {
                            FURI_LOG_I(TAG, "ICY: metaint=%lu", (unsigned long)rs->icy_metaint);
                        }

                        if(!rs->pcm_started) {
                            FURI_LOG_I(TAG, "Net: starting PCM output");
                            pcm_output_start(rs->output);
                            rs->pcm_started = true;
                            FURI_LOG_I(TAG, "Net: PCM started");
                        }

                        data = header_buf + i + 4;
                        data_len = header_size - (i + 4);
                        break;
                    }
                }
                if(!headers_done) continue;
            }

            if(data_len > 0) {
                radio_on_icy_stream(rs, data, data_len);
            }

            radio_decode_and_output(rs, false);
            furi_thread_yield();
        }
        FURI_LOG_I(TAG, "Net: loop done (recv=%luKB, timeouts=%lu)",
                   (unsigned long)(total_recv / 1024), (unsigned long)total_sel_timeouts);
    }

close_sock:
    FURI_LOG_I(TAG, "Net: closing socket");
    close(sock);
    sock = -1;

cleanup:
    FURI_LOG_I(TAG, "Net: deinitializing network");
    network_deinit_current_thread(rs->network);
    furi_record_close(RECORD_NETWORK);

cleanup_nonet:
    if(recv_buf) free(recv_buf);
    if(header_buf) free(header_buf);
    if(host) free(host);
    rs->playing = false;
    FURI_LOG_I(TAG, "Net: thread done");
    return 0;
}

static int32_t radio_file_thread(void* context) {
    RadioStream* rs = context;

    pcm_output_start(rs->output);
    pcm_output_set_rebuffer_enabled(rs->output, false);
    rs->pcm_started = true;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);

    const char* path = furi_string_get_cstr(rs->url);
    if(!storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        FURI_LOG_E(TAG, "File: cannot open %s", path);
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        rs->playing = false;
        return 1;
    }

    FURI_LOG_I(TAG, "File: opened %s, size %llu", path, storage_file_size(file));

    uint8_t* read_buf = malloc(2048);
    if(!read_buf) {
        FURI_LOG_E(TAG, "File: malloc failed");
        storage_file_close(file);
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        rs->playing = false;
        return 1;
    }

    bool eof = false;
    while(!rs->stop_requested) {
        if(!eof) {
            size_t space = mp3_decoder_space(rs->decoder);
            if(space > 0) {
                size_t to_read = (space < 2048) ? space : 2048;
                uint16_t bytes_read = storage_file_read(file, read_buf, to_read);
                if(bytes_read == 0) {
                    eof = true;
                    FURI_LOG_I(TAG, "File: EOF");
                } else {
                    mp3_decoder_feed(rs->decoder, read_buf, bytes_read);
                }
            }
        }

        radio_decode_and_output(rs, eof);

        if(eof && mp3_decoder_buffered(rs->decoder) < 4) {
            FURI_LOG_I(TAG, "File: all data decoded");
            break;
        }

        if(pcm_output_free_space(rs->output) < 1152) {
            furi_delay_ms(2);
        }
    }

    /* Drain remaining PCM */
    while(!rs->stop_requested && pcm_output_is_active(rs->output) &&
          pcm_output_available(rs->output) > 0) {
        furi_delay_ms(10);
    }
    if(pcm_output_is_active(rs->output)) furi_delay_ms(100);

    free(read_buf);
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    rs->playing = false;
    FURI_LOG_I(TAG, "File: done");
    return 0;
}

RadioStream* radio_stream_alloc(void) {
    RadioStream* rs = malloc(sizeof(RadioStream));
    if(!rs) {
        FURI_LOG_E(TAG, "Alloc failed");
        return NULL;
    }
    memset(rs, 0, sizeof(RadioStream));

    rs->decoder = mp3_decoder_alloc();
    rs->output = pcm_output_alloc();
    rs->decode_buf = malloc(MP3_DECODER_MAX_OUTPUT_SAMPLES * sizeof(int16_t));
    rs->url = furi_string_alloc();
    rs->volume = 1.0f;
    rs->icy_mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    if(!rs->decoder || !rs->output || !rs->decode_buf || !rs->url || !rs->icy_mutex) {
        FURI_LOG_E(TAG, "Sub-allocation failed");
        mp3_decoder_free(rs->decoder);
        pcm_output_free(rs->output);
        free(rs->decode_buf);
        if(rs->url) furi_string_free(rs->url);
        if(rs->icy_mutex) furi_mutex_free(rs->icy_mutex);
        free(rs);
        return NULL;
    }

    return rs;
}

void radio_stream_free(RadioStream* rs) {
    if(!rs) return;
    radio_stream_stop(rs);
    mp3_decoder_free(rs->decoder);
    pcm_output_free(rs->output);
    if(rs->decode_buf) free(rs->decode_buf);
    furi_string_free(rs->url);
    if(rs->icy_mutex) furi_mutex_free(rs->icy_mutex);
    free(rs);
}

bool radio_stream_play(RadioStream* rs, const char* url) {
    furi_check(rs);
    if(!url || url[0] == '\0') return false;

    radio_stream_stop(rs);

    FURI_LOG_I(TAG, "Play: %s", url);
    furi_string_set(rs->url, url);

    mp3_decoder_reset(rs->decoder);
    rs->stop_requested = false;
    rs->playing = true;
    rs->last_data_tick = 0;
    rs->last_audio_tick = 0;
    rs->pcm_started = false;

    rs->icy_metaint = 0;
    rs->icy_counter = 0;
    rs->icy_reading_meta = false;
    rs->icy_meta_pos = 0;
    rs->icy_meta_size = 0;
    furi_mutex_acquire(rs->icy_mutex, FuriWaitForever);
    rs->icy_title[0] = '\0';
    rs->icy_name[0] = '\0';
    furi_mutex_release(rs->icy_mutex);

    rs->net_thread = furi_thread_alloc_ex("RadioNet", RS_NET_STACK_SIZE, radio_net_thread, rs);
    if(!rs->net_thread) {
        rs->playing = false;
        return false;
    }
    furi_thread_start(rs->net_thread);
    return true;
}

bool radio_stream_play_file(RadioStream* rs, const char* path) {
    furi_check(rs);
    if(!path || path[0] == '\0') return false;

    radio_stream_stop(rs);

    FURI_LOG_I(TAG, "PlayFile: %s", path);
    furi_string_set(rs->url, path);

    mp3_decoder_reset(rs->decoder);
    rs->stop_requested = false;
    rs->playing = true;
    rs->last_data_tick = 0;
    rs->last_audio_tick = 0;
    rs->pcm_started = false;

    rs->net_thread = furi_thread_alloc_ex("RadioFile", RS_FILE_STACK_SIZE, radio_file_thread, rs);
    if(!rs->net_thread) {
        rs->playing = false;
        return false;
    }
    furi_thread_start(rs->net_thread);
    return true;
}

void radio_stream_stop(RadioStream* rs) {
    furi_check(rs);
    if(!rs->playing && !rs->net_thread) {
        /* Previous stop may have joined the net thread without clearing pcm_started
         * (e.g. stop during pcm_output_start before rs->pcm_started was set). Next
         * play() would then skip pcm_output_start() entirely — silence. */
        if(rs->pcm_started) {
            pcm_output_stop(rs->output);
            rs->pcm_started = false;
        }
        return;
    }

    FURI_LOG_I(TAG, "Stopping...");
    rs->stop_requested = true;

    if(rs->net_thread) {
        furi_thread_join(rs->net_thread);
        furi_thread_free(rs->net_thread);
        rs->net_thread = NULL;
    }

    /* Always teardown PCM after the worker exits: do not gate on pcm_started alone
     * — the net thread can leave SAI/amp touched even if the flag was never set. */
    pcm_output_stop(rs->output);
    rs->pcm_started = false;

    rs->playing = false;
    FURI_LOG_I(TAG, "Stopped");
}

bool radio_stream_is_playing(RadioStream* rs) {
    furi_check(rs);
    return rs->playing && pcm_output_is_active(rs->output);
}

void radio_stream_get_stats(RadioStream* rs, RadioStreamStats* stats) {
    furi_check(rs);
    furi_check(stats);

    const uint32_t now = furi_get_tick();
    const uint32_t last_data_tick = rs->last_data_tick;
    const uint32_t last_audio_tick = rs->last_audio_tick;

    memset(stats, 0, sizeof(RadioStreamStats));
    stats->playing = rs->playing;
    stats->output_active = pcm_output_is_active(rs->output);
    stats->buffering = pcm_output_is_buffering(rs->output);
    stats->has_data = last_data_tick != 0;
    stats->has_audio = last_audio_tick != 0;
    stats->data_age_ms = stats->has_data ? now - last_data_tick : UINT32_MAX;
    stats->audio_age_ms = stats->has_audio ? now - last_audio_tick : UINT32_MAX;
    stats->underrun_count = pcm_output_underrun_count(rs->output);
    stats->pcm_samples = (uint32_t)pcm_output_available(rs->output);
    stats->decoder_bytes = (uint32_t)mp3_decoder_buffered(rs->decoder);
}

void radio_stream_set_volume(RadioStream* rs, float volume) {
    furi_check(rs);
    if(volume < 0.0f) {
        volume = 0.0f;
    } else if(volume > 1.0f) {
        volume = 1.0f;
    }
    rs->volume = volume;
}

const char* radio_stream_get_title(RadioStream* rs) {
    static char title_buf[RS_ICY_TITLE_SIZE];
    furi_check(rs);

    furi_mutex_acquire(rs->icy_mutex, FuriWaitForever);
    strncpy(title_buf, rs->icy_title, sizeof(title_buf) - 1);
    title_buf[sizeof(title_buf) - 1] = '\0';
    furi_mutex_release(rs->icy_mutex);

    return title_buf;
}

const char* radio_stream_get_stream_name(RadioStream* rs) {
    static char name_buf[RS_ICY_NAME_SIZE];
    furi_check(rs);

    furi_mutex_acquire(rs->icy_mutex, FuriWaitForever);
    strncpy(name_buf, rs->icy_name, sizeof(name_buf) - 1);
    name_buf[sizeof(name_buf) - 1] = '\0';
    furi_mutex_release(rs->icy_mutex);

    return name_buf;
}
