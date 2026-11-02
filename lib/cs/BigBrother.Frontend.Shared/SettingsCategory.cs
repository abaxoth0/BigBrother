using System.Windows;
using System.Windows.Markup;

namespace frontend;

[ContentProperty(nameof(Content))]
public sealed class SettingsCategory
{
    public string Title { get; set; } = "";
    public string Description { get; set; } = "";
    public bool ShowSaveButton { get; set; } = true;
    public UIElement Content { get; set; } = null!;
}
