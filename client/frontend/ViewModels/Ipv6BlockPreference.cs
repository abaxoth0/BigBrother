namespace frontend.ViewModels;

internal sealed class Ipv6BlockPreference
{
    private long _revision;
    public bool Value { get; private set; } = true;
    public bool IsEdited { get; private set; }
    public long Revision => Interlocked.Read(ref _revision);

    public bool Edit(bool value)
    {
        if (Value == value) return false;
        Value = value;
        IsEdited = true;
        Interlocked.Increment(ref _revision);
        return true;
    }

    public bool Refresh(bool? value, long revision)
    {
        if (!value.HasValue || IsEdited || revision != Revision || Value == value.Value) return false;
        Value = value.Value;
        return true;
    }

    public void ConfirmSave(long revision)
    {
        if (revision != Revision) return;
        IsEdited = false;
        Interlocked.Increment(ref _revision);
    }
}
