namespace frontend.Services;

public sealed record CertificateInspection(string Address, string Port, string Pinned, string Observed);
