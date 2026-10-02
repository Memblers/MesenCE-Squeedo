using CommunityToolkit.Mvvm.ComponentModel;
using System;

namespace Mesen.Config
{
	public partial class McpConfig : BaseConfig<McpConfig>
	{
		[ObservableProperty] public partial bool Enabled { get; set; } = false;
		[ObservableProperty] [MinMax(1, 65535)] public partial UInt32 Port { get; set; } = 9100;

		public McpConfig()
		{
		}
	}
}
