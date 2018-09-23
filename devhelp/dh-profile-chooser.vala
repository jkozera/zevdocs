using Gdk;
using Gtk;

int _dh_util_surface_scale(int scale)
{
        if (scale == 1)
                return 1;
        else
                return (int)(2.0 * (2.0 / (double)scale));
}

public class DhProfileChooser : Toolbar {

    ToolButton drag_button;
    ToolButton default_button;
    string cur_docset_id;
    private ToggleToolButton[] buttons;
    bool handling_toggle;

    public DhProfileChooser() {
        this.set_style(ToolbarStyle.ICONS);

        this.drag_motion.connect(this.on_drag_motion);
        this.drag_data_received.connect(this.on_drag_data_received);
        this.drag_drop.connect(this.on_drag_drop);
        this.drag_leave.connect(this.on_drag_leave);
        TargetEntry list_targets[] = {TargetEntry(){
            target="zevdocs-docs-with-b64-icon",
            flags=TargetFlags.SAME_APP,
            info=1
        }};
        drag_dest_set(
            this,
            DestDefaults.HIGHLIGHT,
            list_targets,
            DragAction.LINK
        );
        load_groups();
    }

    void bind_toggle_handler(ToggleToolButton btn, int i) {
        btn.toggled.connect(() => {
            if (handling_toggle) return;
            handling_toggle = true;
            for (int j = 0; j < buttons.length; ++j) {
                if (i != j) {
                    buttons[j].set_active(false);
                }
            }
            handling_toggle = false;
        });
    }

    void load_groups() {
        Soup.Session session = new Soup.Session();
        Soup.Request req = session.request("http://localhost:12340/group");
        GLib.InputStream stream = req.send();
        GLib.DataInputStream data_stream = new GLib.DataInputStream(stream);
        Json.Node line_node = Json.from_string(data_stream.read_line()); 
        Json.Array array = line_node.get_array();
        if (array.get_length() == 0) {
            default_button = new ToolButton(null, _("drag&drop here to group"));
            this.add(default_button);
            default_button.set_sensitive(false);
        }
        for (int i = 0; i < buttons.length; ++i) {
            this.remove(buttons[i]);
            buttons[i].destroy();
        }
        buttons = buttons[0:0];
        for (int i = 0; i < array.get_length(); ++i) {
            Json.Object obj = array.get_element(i).get_object();
            string icon = obj.get_string_member("Icon");
            string name = obj.get_string_member("Name");
            ToggleToolButton btn = new ToggleToolButton();
            this.add(btn);
            buttons += btn;
            if (icon.length == 1) {
                btn.set_label(icon);
            } else {
                btn.set_icon_widget(new Image.from_icon_name(icon, IconSize.SMALL_TOOLBAR));
                btn.set_label(name);
            }
            bind_toggle_handler(btn, i);
            btn.show_all();
        }
    }

    bool on_drag_motion(DragContext context, int x, int y, uint time) {
        drag_get_data(this, context, Atom.intern("zevdocs-docs-with-b64-icon", false), time);
        return true;
    }

    void on_drag_data_received(DragContext context, int x, int y, SelectionData data, uint info, uint time) {
        if (drag_button == null) {
            if (data.get_data() == null)
                return;
            drag_status(context, DragAction.LINK, time);
            string data_string = (string) data.get_data();
            string[] splitted = data_string.split(";", 2);
            cur_docset_id = splitted[0];
            uchar[] decoded = Base64.decode(splitted[1]);
            MemoryInputStream istream = new MemoryInputStream.from_data(decoded);
            Pixbuf pixbuf = new Pixbuf.from_stream(istream);
            Cairo.Surface surface = cairo_surface_create_from_pixbuf(
                pixbuf, _dh_util_surface_scale(this.get_scale_factor()), null
            );
            Image *image = new Image.from_surface(surface);
            drag_button = new ToolButton(image, null);
            this.insert(drag_button, -1);
            drag_button.show_all();
            if (default_button != null)
                default_button.hide();
            drag_highlight(this);
        }
    }

    bool on_drag_drop(DragContext context, int x, int y, uint time) {
        DhGroupDialog dialog = new DhGroupDialog(cur_docset_id);
        Gtk.Window parent_window = (Gtk.Window) this.get_toplevel();
        dialog.set_transient_for(parent_window);
        if (dialog.run() == ResponseType.OK) {
                print("%s/%s/'%s'\n",
                      dialog.get_current_text(),
                      dialog.get_current_icon(),
                      dialog.get_group_id());
                string current_text = dialog.get_current_text();
                string current_icon = dialog.get_current_icon();
                load_groups();
        }
        dialog.destroy();
        cur_docset_id = null;
        return true;
    }

    void on_drag_leave(DragContext context, uint time) {
        if (drag_button != null) {
            drag_unhighlight(this);
            drag_button.destroy();
            if (default_button != null)
                default_button.show();
            drag_button = null;
        }
    }
}