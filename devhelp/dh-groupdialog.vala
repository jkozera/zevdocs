using Gtk;

[GtkTemplate (ui="/org/gnome/devhelp/dh-groupdialog.ui")]
public class DhGroupDialog : Dialog {
    [GtkChild]
    private FlowBox icons_flow_box;

    [GtkChild]
    private ScrolledWindow icons_scrolled_window;

    private Button name_button;

    private string current_text;
    private string current_letter;

    public DhGroupDialog (string docset_id) {
        print(docset_id);
        foreach (string s in IconTheme.get_default().list_icons("Categories")) {
            Image image = new Image.from_icon_name(s, IconSize.LARGE_TOOLBAR);
            this.icons_flow_box.add(image);
            image.show();
        }

        CssProvider css = new CssProvider();
        css.load_from_data("* { padding: 2pt 2pt; }");
        Box bbox = new Box(Gtk.Orientation.HORIZONTAL, 1);
        this.name_button = new Button();
        bbox.add(this.name_button);
        bbox.set_halign(Gtk.Align.CENTER);
        this.name_button.set_label("A");
        this.name_button.clicked.connect(this.name_button_clicked);
        bbox.show_all();
        this.name_button.get_style_context().add_provider(css, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION);
        this.icons_flow_box.insert(bbox, 0);
        this.current_text = "";
        this.current_letter = "A";
    }

    private void name_button_clicked () {
        this.icons_flow_box.select_child(
            this.icons_flow_box.get_child_at_index(0)
        );
    }

    [GtkCallback]
    private void text_changed (Editable widget) {
        this.current_text = widget.get_chars();
        if (current_text != "") {
            current_letter = current_text.slice(0, 1);
            this.name_button.set_label(current_letter);
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
        FlowBoxChild fb_child = this.icons_flow_box.get_selected_children().first().data;
        if (fb_child.get_index() == 0) {
            return current_letter;
        }
        Image image = (Image)(fb_child.get_child());
        IconSize size;
        string icon;
        image.get_icon_name(out icon, out size);
        return icon;
    }
}
