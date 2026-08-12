using System.Windows;
using frontend.ViewModels;
using frontend.Views;

namespace frontend.Services;

public class WhitelistDialogService : IWhitelistDialogService
{
    public Task<WhitelistDialogResult?> ShowDialogAsync(
        WhitelistDialogMode mode,
        string? initialName = null,
        List<string>? initialEntries = null)
    {
        var dialog = new WhitelistEditWindow { Owner = Application.Current.MainWindow };

        if (mode == WhitelistDialogMode.Create)
            dialog.ViewModel.SetCreateMode();
        else
            dialog.ViewModel.SetEditMode(initialName ?? "", initialEntries ?? new List<string>());

        WhitelistDialogResult? result = null;
        dialog.ViewModel.Saved += (s, e) =>
        {
            result = new WhitelistDialogResult
            {
                Name = dialog.ViewModel.WhitelistName,
                Entries = dialog.ViewModel.GetEntries()
            };
            dialog.DialogResult = true;
        };

        dialog.ShowDialog();
        return Task.FromResult(result);
    }
}
