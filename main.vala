using Gtk;
using GLib;

public class BrowserControllerWindow : Window {
    private Entry url_entry;
    private ComboBoxText bookmark_combo;
    private string socket_path;
    private string bookmarks_file_path;

    public BrowserControllerWindow (string socket, int browser_pid) {
        Object (title: "Mu Browser Control", default_width: 900, default_height: 50);
        set_border_width (6);

        this.socket_path = socket;
        bookmarks_file_path = Path.build_filename (Environment.get_home_dir (), ".mu-bookmarks");

        var box = new Box (Orientation.HORIZONTAL, 6);

        var btn_back = new Button.with_label ("◀");
        var btn_forward = new Button.with_label ("▶");
        var btn_reload = new Button.with_label ("⟳");
        var btn_toggle_tb = new Button.with_label ("👁 Toolbar");

        bookmark_combo = new ComboBoxText ();
        load_bookmarks ();

        bookmark_combo.changed.connect (() => {
            string? text = bookmark_combo.get_active_text ();
            if (text != null && text != "Bookmarks..." && text.contains (" | ")) {
                string[] parts = text.split (" | ");
                if (parts.length > 1) {
                    url_entry.text = parts[1].strip ();
                    trigger_navigation (url_entry.text);
                }
            }
        });

        var btn_edit_bookmarks = new Button.with_label ("✏");
        btn_edit_bookmarks.tooltip_text = "Edit bookmarks";
        btn_edit_bookmarks.clicked.connect (open_bookmark_editor);

        url_entry = new Entry ();
        url_entry.hexpand = true;
        url_entry.text = "https://duckduckgo.com";

        var btn_go = new Button.with_label ("Go");

        box.pack_start (btn_back, false, false, 0);
        box.pack_start (btn_forward, false, false, 0);
        box.pack_start (btn_reload, false, false, 0);
        box.pack_start (btn_toggle_tb, false, false, 0);
        box.pack_start (bookmark_combo, false, false, 0);
        box.pack_start (btn_edit_bookmarks, false, false, 0);
        box.pack_start (url_entry, true, true, 0);
        box.pack_start (btn_go, false, false, 0);

        add (box);

        btn_back.clicked.connect (() => { send_cmd.begin ("{\"cmd\":\"back\"}"); });
        btn_forward.clicked.connect (() => { send_cmd.begin ("{\"cmd\":\"forward\"}"); });
        btn_reload.clicked.connect (() => { send_cmd.begin ("{\"cmd\":\"reload\"}"); });
        btn_toggle_tb.clicked.connect (() => { send_cmd.begin ("{\"cmd\":\"toggle-toolbar\"}"); });

        btn_go.clicked.connect (() => {
            string url = url_entry.text.strip ();
            trigger_navigation (url);
        });

        url_entry.activate.connect (() => {
            btn_go.clicked ();
        });

        // Prüft alle 300ms über die PID, ob das Qt-Backend noch läuft
        if (browser_pid > 0) {
            Timeout.add (300, () => {
                if (Posix.kill (browser_pid, 0) != 0) {
                    Gtk.main_quit ();
                    return false;
                }
                return true;
            });
        }

        this.destroy.connect (() => {
            send_cmd_fire_and_forget ("{\"cmd\":\"quit\"}");
            Gtk.main_quit ();
        });
    }

    private void trigger_navigation (string input_url) {
        string url = input_url.strip ();
        if (!url.has_prefix ("http://") && !url.has_prefix ("https://")) {
            url = "https://" + url;
        }
        send_cmd.begin (@"{\"cmd\":\"navigate\",\"url\":\"$url\"}");
    }

    private void load_bookmarks () {
        bookmark_combo.remove_all ();
        bookmark_combo.append_text ("Bookmarks...");
        bookmark_combo.set_active (0);

        var file = File.new_for_path (bookmarks_file_path);
        if (!file.query_exists ()) {
            try {
                var dis = new DataOutputStream (file.create (FileCreateFlags.NONE));
                dis.put_string ("DuckDuckGo | https://duckduckgo.com\n");
            } catch (Error e) {
                warning ("Could not create default bookmarks: %s", e.message);
            }
        }

        try {
            var dis = new DataInputStream (file.read ());
            string line;
            while ((line = dis.read_line (null)) != null) {
                line = line.strip ();
                if (line.length > 0 && !line.has_prefix ("#")) {
                    bookmark_combo.append_text (line);
                }
            }
        } catch (Error e) {
            warning ("Could not read bookmarks: %s", e.message);
        }
    }

    private void open_bookmark_editor () {
        var dialog = new Dialog.with_buttons ("Edit Bookmarks", this, DialogFlags.MODAL, "_Cancel", ResponseType.CANCEL, "_Save", ResponseType.ACCEPT, null);
        dialog.set_default_size (500, 350);

        var text_view = new TextView ();
        text_view.monospace = true;
        
        var scroll = new ScrolledWindow (null, null);
        scroll.set_policy (PolicyType.AUTOMATIC, PolicyType.AUTOMATIC);
        scroll.add (text_view);

        var content_area = dialog.get_content_area ();
        content_area.pack_start (scroll, true, true, 0);

        var file = File.new_for_path (bookmarks_file_path);
        if (file.query_exists ()) {
            try {
                uint8[] contents;
                file.load_contents (null, out contents, null);
                text_view.buffer.text = (string) contents;
            } catch (Error e) {
                warning ("Could not load file: %s", e.message);
            }
        }

        dialog.show_all ();

        if (dialog.run () == ResponseType.ACCEPT) {
            TextIter start, end;
            text_view.buffer.get_bounds (out start, out end);
            string new_text = text_view.buffer.get_text (start, end, false);

            try {
                var dos = new DataOutputStream (file.replace (null, false, FileCreateFlags.NONE));
                dos.put_string (new_text);
                load_bookmarks ();
            } catch (Error e) {
                warning ("Could not save bookmarks: %s", e.message);
            }
        }

        dialog.destroy ();
    }

    private async void send_cmd (string json_payload) {
        var client = new SocketClient ();
        try {
            var conn = yield client.connect_async (new UnixSocketAddress (socket_path), null);
            string msg = json_payload + "\n";
            yield conn.get_output_stream ().write_all_async (msg.data, Priority.DEFAULT, null, null);
        } catch (Error e) {
            warning ("IPC Error: %s", e.message);
        }
    }

    private void send_cmd_fire_and_forget (string json_payload) {
        try {
            var socket_address = new UnixSocketAddress (socket_path);
            var client = new SocketClient ();
            var conn = client.connect (socket_address, null);
            string msg = json_payload + "\n";
            conn.get_output_stream ().write_all (msg.data, null);
        } catch (Error e) {
            // Ignore if backend already closed
        }
    }

    public static async void send_cmd_sync_helper (string socket_path, string json_payload) {
        var client = new SocketClient ();
        try {
            var conn = yield client.connect_async (new UnixSocketAddress (socket_path), null);
            string msg = json_payload + "\n";
            yield conn.get_output_stream ().write_all_async (msg.data, Priority.DEFAULT, null, null);
        } catch (Error e) {
            stderr.printf ("IPC Error: %s\n", e.message);
        }
    }

    public static int main (string[] args) {
        if (args.length > 2 && args[2].has_prefix ("{") || (args.length > 2 && int.parse (args[2]) == 0 && args[2] != "0")) {
            string socket = args[1];
            string cmd_arg = args[2];
            string json_payload = cmd_arg;
            if (!cmd_arg.has_prefix ("{")) {
                json_payload = "{\"cmd\":\"" + cmd_arg + "\"}";
            }
            
            var loop = new MainLoop ();
            send_cmd_sync_helper.begin (socket, json_payload, () => {
                loop.quit ();
            });
            loop.run ();
            return 0;
        }

        Gtk.init (ref args);
        
        string socket = "/tmp/mu-browser.sock";
        int pid = 0;

        if (args.length > 1) {
            socket = args[1];
        }
        if (args.length > 2) {
            pid = int.parse (args[2]);
        }

        var win = new BrowserControllerWindow (socket, pid);
        win.show_all ();
        Gtk.main ();
        return 0;
    }
}