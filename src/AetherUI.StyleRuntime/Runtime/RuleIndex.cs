namespace AetherUI.StyleRuntime;

internal sealed class RuleIndex
{
    private readonly Dictionary<string, List<CompiledRule>> _byId = new(StringComparer.Ordinal);
    private readonly Dictionary<string, List<CompiledRule>> _byClass = new(StringComparer.Ordinal);
    private readonly Dictionary<string, List<CompiledRule>> _byType = new(StringComparer.OrdinalIgnoreCase);
    private readonly List<CompiledRule> _universal = [];

    public SelectorDependency Dependencies { get; private set; }

    public void Rebuild(IEnumerable<CompiledStyleSheet> sheets)
    {
        _byId.Clear();
        _byClass.Clear();
        _byType.Clear();
        _universal.Clear();
        Dependencies = SelectorDependency.None;

        foreach (var sheet in sheets.OrderBy(static item => item.Layer).ThenBy(static item => item.Handle.Value))
        {
            foreach (var rule in sheet.Rules)
            {
                Add(rule);
            }
        }
    }

    public IReadOnlyList<CompiledRule> GetCandidates(IAetherStyleNode node)
    {
        var result = new List<CompiledRule>();
        var seen = new HashSet<int>();

        void AddRange(IEnumerable<CompiledRule> rules)
        {
            foreach (var rule in rules)
            {
                if (seen.Add(rule.RuleId))
                {
                    result.Add(rule);
                }
            }
        }

        if (node.StyleId is not null && _byId.TryGetValue(node.StyleId, out var idRules))
        {
            AddRange(idRules);
        }

        foreach (var className in node.StyleClasses)
        {
            if (_byClass.TryGetValue(className, out var classRules))
            {
                AddRange(classRules);
            }
        }

        if (_byType.TryGetValue(node.StyleType, out var typeRules))
        {
            AddRange(typeRules);
        }

        AddRange(_universal);
        return result;
    }

    private void Add(CompiledRule rule)
    {
        Dependencies |= rule.Selector.Dependencies;
        var anchor = rule.Selector.Anchor;
        if (anchor.Id is not null)
        {
            AddTo(_byId, anchor.Id, rule);
        }
        else if (anchor.ClassName is not null)
        {
            AddTo(_byClass, anchor.ClassName, rule);
        }
        else if (anchor.TypeName is not null)
        {
            AddTo(_byType, anchor.TypeName, rule);
        }
        else
        {
            _universal.Add(rule);
        }
    }

    private static void AddTo(
        Dictionary<string, List<CompiledRule>> index,
        string key,
        CompiledRule rule)
    {
        if (!index.TryGetValue(key, out var bucket))
        {
            bucket = [];
            index.Add(key, bucket);
        }

        bucket.Add(rule);
    }
}
