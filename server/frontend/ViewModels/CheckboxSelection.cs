using frontend.Models;

namespace frontend.ViewModels;

public class CheckboxSelection<T> where T : class, ISelectableRow
{
    private T? _anchor;

    public bool HandleClick(IList<T> items, T item, bool control, bool shift)
    {
        if (!control && !shift)
        {
            _anchor = item;
            return false;
        }

        if (shift)
        {
            var end = items.IndexOf(item);
            if (end < 0) return false;
            var start = _anchor == null ? end : items.IndexOf(_anchor);
            if (start < 0) start = end;
            for (var i = 0; i < items.Count; i++)
                items[i].IsSelected = (i >= Math.Min(start, end) && i <= Math.Max(start, end)) ||
                    (control && items[i].IsSelected);
            _anchor = items[start];
        }
        else
        {
            item.IsSelected = !item.IsSelected;
            _anchor = item;
        }

        return true;
    }
}
