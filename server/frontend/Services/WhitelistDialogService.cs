using System.Windows;
using frontend.ViewModels;
using frontend.Views;

namespace frontend.Services;

public class WhitelistDialogService : IWhitelistDialogService
{
    public Task ShowDialogAsync(
        WhitelistDialogMode mode,
        Func<WhitelistDialogResult, Task<bool>> saveAsync,
        string? initialName = null,
        List<string>? initialEntries = null)
    {
        var dialog = new WhitelistEditWindow { Owner = Application.Current.MainWindow };

        if (mode == WhitelistDialogMode.Create)
            dialog.ViewModel.SetCreateMode();
        else
            dialog.ViewModel.SetEditMode(initialName ?? "", initialEntries ?? new List<string>());

        dialog.Closing += (_, e) => e.Cancel = dialog.ViewModel.IsSaving;
        dialog.ViewModel.Saved += async (_, e) =>
        {
            var result = new WhitelistDialogResult
            {
                Name = dialog.ViewModel.WhitelistName,
                Entries = dialog.ViewModel.GetEntries()
            };
            dialog.ViewModel.IsSaving = true;
            try
            {
                if (!await saveAsync(result))
                {
                    MessageBox.Show(dialog, "Не удалось сохранить список. Повторите попытку.",
                        "Сохранение списка", MessageBoxButton.OK, MessageBoxImage.Error);
                    return;
                }
                dialog.ViewModel.MarkSaved(result.Name);
                dialog.ViewModel.IsSaving = false;
                if (e.CloseAfterSave) dialog.DialogResult = true;
            }
            catch (Exception ex)
            {
                MessageBox.Show(dialog, $"Не удалось сохранить список.\n{ex.Message}",
                    "Сохранение списка", MessageBoxButton.OK, MessageBoxImage.Error);
            }
            finally
            {
                dialog.ViewModel.IsSaving = false;
            }
        };

        dialog.ShowDialog();
        return Task.CompletedTask;
    }
}
