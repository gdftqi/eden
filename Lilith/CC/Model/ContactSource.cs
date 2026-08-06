using System;
using System.Collections.Generic;

namespace CC.Model
{
    /// <summary>
    /// 一个联系人.目前只有界面用得到的字段, 等服务端接口定了再补 uid 等.
    /// </summary>
    public sealed class ContactInfo
    {
        public string Nickname { get; init; } = string.Empty;
        public string Sign { get; init; } = string.Empty;

        /// <summary>
        /// 排序键(小写拼音, 英文名直接用它自己).用于 A-Z 分组与搜索.
        ///
        /// 刻意不在客户端算拼音: 中文转拼音要么带一张两万字的表, 要么引第三方库,
        /// 两者都不该由客户端承担.正规做法是服务端下发这个字段, 现在示例数据手填.
        /// </summary>
        public string Sort { get; init; } = string.Empty;


        /// <summary>
        /// 该联系人归到哪个索引字母下: A-Z / 0-9 归 #(数字) / 其余归 #.
        /// Sort 为空时一律归 #, 不猜.
        /// </summary>
        public char IndexKey
        {
            get
            {
                if (Sort.Length == 0)
                {
                    return '#';
                }

                char c = char.ToUpperInvariant(Sort[0]);
                return (c >= 'A' && c <= 'Z') ? c : '#';
            }
        }
    }


    /// <summary>
    /// 联系人数据源.
    /// </summary>
    public static class ContactSource
    {
        public static IReadOnlyList<ContactInfo> All { get; } = Build();


        private static List<ContactInfo> Build()
        {
            var list = new List<ContactInfo>
            {
                new ContactInfo { Nickname = "美女1",   Sign = "今天也要加油鸭", Sort = "meinv1" },
                new ContactInfo { Nickname = "美女2",   Sign = "",               Sort = "meinv2" },
                new ContactInfo { Nickname = "美女3",   Sign = "忙, 勿扰",       Sort = "meinv3" },
                new ContactInfo { Nickname = "阿强",     Sign = "在忙",           Sort = "aqiang" },
                new ContactInfo { Nickname = "陈小二",   Sign = "",               Sort = "chenxiaoer" },
                new ContactInfo { Nickname = "Kevin",   Sign = "PM",             Sort = "kevin" },
                new ContactInfo { Nickname = "老王",     Sign = "隔壁",           Sort = "laowang" },
                new ContactInfo { Nickname = "张三",     Sign = "",               Sort = "zhangsan" },
                new ContactInfo { Nickname = "007",     Sign = "特工",           Sort = "007" },
            };

            for (int i = 1; i <= 15; i++)
            {
                list.Add(new ContactInfo
                {
                    Nickname = $"联系人 {i}",
                    Sign = "这是一条示例签名",
                    Sort = $"lianxiren{i:D2}",
                });
            }

            return list;
        }
    }
}
