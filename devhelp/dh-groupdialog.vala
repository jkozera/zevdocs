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

    public DhGroupDialog () {
        foreach (string context in ENABLED_CONTEXTS) {
            foreach (string s in IconTheme.get_default().list_icons("Categories")) {
                Image image = new Image.from_icon_name(s, IconSize.LARGE_TOOLBAR);
                this.icons_flow_box.add(image);
                image.show();
            }
        }
        CssProvider css = new CssProvider();
        css.load_from_data("* { padding: 10 10; }");
        this.icon_button.get_style_context().add_provider(
            css, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
        );
    }

    [GtkCallback]
    private void text_changed (Editable widget) {
        if (widget.get_chars() == "") {
            this.icon_stack.set_visible_child(this.icons_scrolled_window);
        } else {
            this.icon_button.label = widget.get_chars().slice(0, 1);
            this.icon_stack.set_visible_child(this.icon_button);
        }
    }
}
