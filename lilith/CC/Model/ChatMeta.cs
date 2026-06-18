using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace CC.Model
{
    public class ChatMeta
    {
        public string SessionID { get; set; }
        public int ChatType { get; set; }
        public string PeerID { get; set; }
        public string PeerNickname { get; set; }
        public ulong SyncSeq { get; set; }
        public ulong ReadSeq { get; set; }
        public ulong PinSeq { get; set; }

        public const string CREATE_TABLE = """
            CREATE TABLE IF NOT EXISTS CHAT_META(
            SESSION_ID TEXT NOT NULL,
            CHAT_TYPE INTEGER NOT NULL DEFAULT 1,
            PEER_ID TEXT NOT NULL,
            PEER_NAME TEXT NOT NULL,
            SYNC_SEQ INTEGER NOT NULL DEFAULT 0,
            READ_SEQ INTEGER NOT NULL DEFAULT 0,
            PIN_SEQ INTEGER NOT NULL DEFAULT 0,
            PRIMARY KEY (SESSION_ID)
        );
        """;
    }
}
