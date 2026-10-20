using frontend.Models;

namespace frontend.ViewModels;

public class WhitelistSelection
{
    private WhitelistInfo? _anchor;

    public bool HandleClick(IList<WhitelistInfo> items, WhitelistInfo whitelist, bool control, bool shift)
    {
        if (!control && !shift)
        {
            _anchor = whitelist;
            return false;
        }

        if (shift)
        {
            var end = items.IndexOf(whitelist);
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
            whitelist.IsSelected = !whitelist.IsSelected;
            _anchor = whitelist;
        }

        return true;
    }
}
