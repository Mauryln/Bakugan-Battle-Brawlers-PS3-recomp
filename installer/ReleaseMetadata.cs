using System.Reflection;

[assembly: AssemblyVersion(ReleaseMetadata.Version + ".0")]
[assembly: AssemblyFileVersion(ReleaseMetadata.Version + ".0")]
[assembly: AssemblyInformationalVersion(ReleaseMetadata.Version)]

internal static class ReleaseMetadata
{
    internal const string Version = "1.3.5";
}
