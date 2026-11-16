// The saves in INS_Saves (ours and the base mod's) carry no world name, so a save from another map, or one orphaned
// from the base mod's own cache save, could stop a mission recovering. The folder is cleared so the mission starts
// clean when it was written by a different world, or when the base mod's cache save is gone.
class INF_SaveWorldGuard
{
	static const string DIRECTORY = "$profile:INS_Saves";
	static const string MARKER_NAME = "world.dat";
	static const string MARKER = "$profile:INS_Saves/world.dat";
	protected static bool s_bChecked;
	protected ref array<string> m_aFound = {};

	static string WorldKey(string world)
	{
		string key = world;
		array<string> unsafe = {"\\", "/", ":", "{", "}", "<", ">", "\"", "|", "?", "*", " "};
		foreach (string character : unsafe)
			key.Replace(character, "_");
		return key;
	}

	// Idempotent; safe to call from every startup entry point. Server only.
	static void Ensure()
	{
		if (s_bChecked || !Replication.IsServer())
			return;
		string current = WorldKey(GetGame().GetWorldFile());
		// The world file may not be known yet at the earliest call; a later call does the check.
		if (current.IsEmpty())
			return;
		s_bChecked = true;
		FileIO.MakeDirectory(DIRECTORY);
		INF_SaveWorldGuard guard = new INF_SaveWorldGuard();
		string saved = ReadMarker();
		Print("[INF_SaveWorldGuard] World " + current + "; saves belong to " + saved + ".");
		if (!saved.IsEmpty() && saved != current)
			guard.Clear("the saves were written by world " + saved + ", not " + current);
		else if (!FileIO.FileExists(INS_CACHE_SAVE_FILE))
			guard.Clear("the base mod's cache save is missing");
		if (saved != current)
			WriteMarker(current);
	}

	protected static string ReadMarker()
	{
		string world;
		if (!FileIO.FileExists(MARKER))
			return string.Empty;
		FileSerializer file = new FileSerializer();
		if (!file.Open(MARKER, FileMode.READ))
			return string.Empty;
		if (!file.Read(world))
			world = string.Empty;
		file.Close();
		return world;
	}

	protected static void WriteMarker(string world)
	{
		FileSerializer file = new FileSerializer();
		if (!file.Open(MARKER, FileMode.WRITE))
		{
			Print("[INF_SaveWorldGuard] Cannot write the world marker: " + MARKER, LogLevel.ERROR);
			return;
		}
		file.Write(world);
		file.Close();
	}

	void OnFile(string path, FileAttribute attributes = 0, string filesystem = string.Empty)
	{
		if (attributes & FileAttribute.DIRECTORY)
			return;
		int cut = Math.Max(path.LastIndexOf("/"), path.LastIndexOf("\\"));
		if (cut >= 0)
			path = path.Substring(cut + 1, path.Length() - cut - 1);
		if (!path.IsEmpty() && path != MARKER_NAME)
			m_aFound.Insert(path);
	}

	// Deletes every save in the folder except the world marker, so the mission starts clean.
	protected void Clear(string reason)
	{
		m_aFound.Clear();
		FileIO.FindFiles(OnFile, DIRECTORY + "/", "");
		if (m_aFound.IsEmpty())
			return;
		Print("[INF_SaveWorldGuard] Clearing " + m_aFound.Count().ToString() + " saves for a clean start: " + reason + ".", LogLevel.WARNING);
		foreach (string name : m_aFound)
		{
			if (!FileIO.DeleteFile(DIRECTORY + "/" + name))
				Print("[INF_SaveWorldGuard] Cannot delete stale save: " + name, LogLevel.ERROR);
		}
	}
}
