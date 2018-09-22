using Gtk;

[GtkTemplate (ui="/org/gnome/devhelp/dh-groupdialog.ui")]
public class DhGroupDialog : Dialog {
    [GtkChild]
    private FlowBox icons_flow_box;

    [GtkChild]
    private ScrolledWindow icons_scrolled_window;

    [GtkChild]
    private Button icon_button;

    [GtkChild]
    private Stack icon_stack;

    private const string ENABLED_CONTEXTS[] = {"Categories", "Applications", "Devices"};

    private string current_text;

    public DhGroupDialog () {
        foreach (string context in ENABLED_CONTEXTS) {
            foreach (string s in IconTheme.get_default().list_icons("Categories")) {
                Image image = new Image.from_icon_name(s, IconSize.LARGE_TOOLBAR);
                this.icons_flow_box.add(image);
                image.show();
            }
        }
        CssProvider css = new CssProvider();
        css.load_from_data("* { padding: 2pt 2pt; }");
        this.icon_button.get_style_context().add_provider(
            css, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
        );
        this.current_text = "";
    }

    [GtkCallback]
    private void text_changed (Editable widget) {
        this.current_text = widget.get_chars();
        if (current_text == "") {
            this.icon_stack.set_visible_child(this.icons_scrolled_window);
        } else {
            this.icon_button.label = current_text.slice(0, 1);
            this.icon_stack.set_visible_child(this.icon_button);
        }
    }

    [GtkCallback]
    private void save_clicked () {
        this.response(Gtk.ResponseType.OK);
    }

    [GtkCallback]
    private void cancel_clicked () {
        this.response(Gtk.ResponseType.CANCEL);
    }

    public string get_current_text() {
        return current_text;
    }

    public string get_current_icon() {
        FlowBoxChild fbchild = this.icons_flow_box.get_selected_children().first().data;
        Image image = (Image)(fbchild.get_child());
        IconSize size;
        string icon;
        image.get_icon_name(out icon, out size);
        return icon;
    }
}
