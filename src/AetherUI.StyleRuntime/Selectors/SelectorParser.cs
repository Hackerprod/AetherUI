namespace AetherUI.StyleRuntime;

internal static class SelectorParser
{
    public static IReadOnlyList<CompiledSelector> ParseList(
        string source,
        DiagnosticBag diagnostics,
        int baseOffset)
    {
        var result = new List<CompiledSelector>();
        var runningOffset = 0;
        foreach (var part in TextSplitter.SplitTopLevel(source, ','))
        {
            var localIndex = source.IndexOf(part, runningOffset, StringComparison.Ordinal);
            if (localIndex < 0)
            {
                localIndex = runningOffset;
            }

            runningOffset = Math.Max(localIndex + part.Length, runningOffset);
            if (string.IsNullOrWhiteSpace(part))
            {
                diagnostics.Error("AUISEL001", "Empty selector.", baseOffset + localIndex);
                continue;
            }

            var parser = new Parser(part, diagnostics, baseOffset + localIndex);
            if (parser.TryParse(out var selector))
            {
                result.Add(selector);
            }
        }

        return result;
    }

    private sealed class Parser
    {
        private readonly string _source;
        private readonly DiagnosticBag _diagnostics;
        private readonly int _baseOffset;
        private int _index;
        private Specificity _specificity;
        private SelectorDependency _dependencies;

        public Parser(string source, DiagnosticBag diagnostics, int baseOffset)
        {
            _source = source;
            _diagnostics = diagnostics;
            _baseOffset = baseOffset;
        }

        public bool TryParse(out CompiledSelector selector)
        {
            var compounds = new List<CompoundSelector>();
            var combinators = new List<SelectorCombinator>();
            SkipWhitespace();
            while (_index < _source.Length)
            {
                if (!TryParseCompound(out var compound))
                {
                    selector = null!;
                    return false;
                }

                compounds.Add(compound);
                var hadWhitespace = SkipWhitespace();
                if (_index >= _source.Length)
                {
                    break;
                }

                var combinator = _source[_index] switch
                {
                    '>' => SelectorCombinator.Child,
                    '+' => SelectorCombinator.AdjacentSibling,
                    '~' => SelectorCombinator.GeneralSibling,
                    _ => hadWhitespace ? SelectorCombinator.Descendant : SelectorCombinator.None,
                };

                if (combinator == SelectorCombinator.None)
                {
                    Error("AUISEL002", $"Unexpected character '{_source[_index]}' in selector.", _index);
                    selector = null!;
                    return false;
                }

                if (_source[_index] is '>' or '+' or '~')
                {
                    _index++;
                    SkipWhitespace();
                }

                if (_index >= _source.Length)
                {
                    Error("AUISEL003", "Selector cannot end with a combinator.", _index - 1);
                    selector = null!;
                    return false;
                }

                combinators.Add(combinator);
                _dependencies |= combinator switch
                {
                    SelectorCombinator.Descendant or SelectorCombinator.Child => SelectorDependency.Ancestor,
                    SelectorCombinator.AdjacentSibling or SelectorCombinator.GeneralSibling => SelectorDependency.Sibling,
                    _ => SelectorDependency.None,
                };
            }

            if (compounds.Count == 0)
            {
                selector = null!;
                return false;
            }

            var steps = new List<SelectorStep>(compounds.Count);
            for (var index = compounds.Count - 1; index >= 0; index--)
            {
                var relation = index > 0 ? combinators[index - 1] : SelectorCombinator.None;
                steps.Add(new SelectorStep(compounds[index], relation));
            }

            var rightmost = compounds[^1];
            var anchor = new SelectorAnchor(
                rightmost.Id,
                rightmost.Classes.FirstOrDefault(),
                rightmost.TypeName);
            selector = new CompiledSelector(steps, _specificity, _dependencies, anchor, _source.Trim());
            return true;
        }

        private bool TryParseCompound(out CompoundSelector compound)
        {
            string? typeName = null;
            string? id = null;
            var classes = new List<string>();
            var attributes = new List<AttributeSelector>();
            var pseudos = new List<PseudoSelector>();
            var consumed = false;

            if (_index < _source.Length && _source[_index] == '*')
            {
                consumed = true;
                _index++;
            }
            else if (_index < _source.Length && IsIdentifierStart(_source[_index]))
            {
                typeName = ReadIdentifier().ToLowerInvariant();
                _specificity += new Specificity(0, 0, 1);
                consumed = true;
            }

            while (_index < _source.Length)
            {
                var current = _source[_index];
                if (current == '#')
                {
                    _index++;
                    var value = ReadIdentifier();
                    if (value.Length == 0 || id is not null)
                    {
                        Error("AUISEL004", "Invalid or repeated id selector.", _index);
                        compound = null!;
                        return false;
                    }

                    id = value;
                    _specificity += new Specificity(1, 0, 0);
                    _dependencies |= SelectorDependency.SelfId;
                    consumed = true;
                }
                else if (current == '.')
                {
                    _index++;
                    var value = ReadIdentifier();
                    if (value.Length == 0)
                    {
                        Error("AUISEL005", "Class selector requires a name.", _index);
                        compound = null!;
                        return false;
                    }

                    classes.Add(value);
                    _specificity += new Specificity(0, 1, 0);
                    _dependencies |= SelectorDependency.SelfClass;
                    consumed = true;
                }
                else if (current == '[')
                {
                    if (!TryParseAttribute(out var attribute))
                    {
                        compound = null!;
                        return false;
                    }

                    attributes.Add(attribute);
                    _specificity += new Specificity(0, 1, 0);
                    _dependencies |= SelectorDependency.SelfAttribute;
                    consumed = true;
                }
                else if (current == ':')
                {
                    if (!TryParsePseudo(out var pseudo, out var pseudoSpecificity, out var pseudoDependencies))
                    {
                        compound = null!;
                        return false;
                    }

                    pseudos.Add(pseudo);
                    _specificity += pseudoSpecificity;
                    _dependencies |= pseudoDependencies;
                    consumed = true;
                }
                else
                {
                    break;
                }
            }

            if (!consumed)
            {
                Error("AUISEL006", "Expected a selector component.", _index);
                compound = null!;
                return false;
            }

            compound = new CompoundSelector(typeName, id, classes.ToArray(), attributes.ToArray(), pseudos.ToArray());
            return true;
        }

        private bool TryParseAttribute(out AttributeSelector attribute)
        {
            var opening = _index++;
            SkipWhitespace();
            var name = ReadIdentifier().ToLowerInvariant();
            if (name.Length == 0)
            {
                Error("AUISEL007", "Attribute selector requires a name.", opening);
                attribute = default;
                return false;
            }

            SkipWhitespace();
            if (_index < _source.Length && _source[_index] == ']')
            {
                _index++;
                attribute = new AttributeSelector(name, AttributeMatchOperator.Exists, null, false);
                return true;
            }

            var operation = AttributeMatchOperator.Equals;
            if (!TryReadAttributeOperator(out operation))
            {
                Error("AUISEL008", "Invalid attribute selector operator.", _index);
                attribute = default;
                return false;
            }

            SkipWhitespace();
            var value = ReadAttributeValue();
            if (value is null)
            {
                Error("AUISEL009", "Attribute selector requires a value.", _index);
                attribute = default;
                return false;
            }

            SkipWhitespace();
            var caseInsensitive = false;
            if (_index < _source.Length && _source[_index] != ']')
            {
                var flag = ReadIdentifier();
                if (flag.Equals("i", StringComparison.OrdinalIgnoreCase))
                {
                    caseInsensitive = true;
                }
                else if (!flag.Equals("s", StringComparison.OrdinalIgnoreCase))
                {
                    Error("AUISEL010", "Unknown attribute comparison flag.", _index);
                    attribute = default;
                    return false;
                }

                SkipWhitespace();
            }

            if (_index >= _source.Length || _source[_index] != ']')
            {
                Error("AUISEL011", "Unterminated attribute selector.", opening);
                attribute = default;
                return false;
            }

            _index++;
            attribute = new AttributeSelector(name, operation, value, caseInsensitive);
            return true;
        }

        private bool TryParsePseudo(
            out PseudoSelector pseudo,
            out Specificity specificity,
            out SelectorDependency dependencies)
        {
            var opening = _index++;
            if (_index < _source.Length && _source[_index] == ':')
            {
                Error("AUISEL012", "Pseudo-elements are not part of the Aether style runtime.", opening);
                pseudo = null!;
                specificity = default;
                dependencies = default;
                return false;
            }

            var name = ReadIdentifier().ToLowerInvariant();
            if (name.Length == 0)
            {
                Error("AUISEL013", "Pseudo selector requires a name.", opening);
                pseudo = null!;
                specificity = default;
                dependencies = default;
                return false;
            }

            string? body = null;
            if (_index < _source.Length && _source[_index] == '(')
            {
                if (!TryReadBalancedBody(out body))
                {
                    Error("AUISEL014", $"Unterminated :{name}() selector.", opening);
                    pseudo = null!;
                    specificity = default;
                    dependencies = default;
                    return false;
                }
            }

            if (TryMapState(name, out var required, out var forbidden))
            {
                if (body is not null)
                {
                    return InvalidPseudo(name, opening, out pseudo, out specificity, out dependencies);
                }

                if (required != StylePseudoState.None)
                {
                    pseudo = new PseudoSelector(PseudoSelectorKind.RequiredState, required, default, Array.Empty<CompiledSelector>());
                }
                else
                {
                    pseudo = new PseudoSelector(PseudoSelectorKind.ForbiddenState, forbidden, default, Array.Empty<CompiledSelector>());
                }

                specificity = new Specificity(0, 1, 0);
                dependencies = SelectorDependency.SelfState;
                return true;
            }

            if (body is null)
            {
                var kind = name switch
                {
                    "root" => PseudoSelectorKind.Root,
                    "empty" => PseudoSelectorKind.Empty,
                    "first-child" => PseudoSelectorKind.FirstChild,
                    "last-child" => PseudoSelectorKind.LastChild,
                    "only-child" => PseudoSelectorKind.OnlyChild,
                    _ => (PseudoSelectorKind)byte.MaxValue,
                };

                if (kind == (PseudoSelectorKind)byte.MaxValue)
                {
                    Error("AUISEL015", $"Unsupported pseudo selector ':{name}'.", opening);
                    pseudo = null!;
                    specificity = default;
                    dependencies = default;
                    return false;
                }

                pseudo = new PseudoSelector(kind, StylePseudoState.None, default, Array.Empty<CompiledSelector>());
                specificity = new Specificity(0, 1, 0);
                dependencies = kind is PseudoSelectorKind.FirstChild or PseudoSelectorKind.LastChild or
                    PseudoSelectorKind.OnlyChild or PseudoSelectorKind.Empty
                    ? SelectorDependency.Structure
                    : SelectorDependency.None;
                return true;
            }

            if (name == "nth-child")
            {
                if (!TryParseNth(body, out var nth))
                {
                    Error("AUISEL016", "Invalid :nth-child() expression.", opening);
                    pseudo = null!;
                    specificity = default;
                    dependencies = default;
                    return false;
                }

                pseudo = new PseudoSelector(PseudoSelectorKind.NthChild, StylePseudoState.None, nth, Array.Empty<CompiledSelector>());
                specificity = new Specificity(0, 1, 0);
                dependencies = SelectorDependency.Structure;
                return true;
            }

            if (name is "is" or "where" or "not" or "has")
            {
                var nested = ParseList(body, _diagnostics, _baseOffset + opening);
                if (nested.Count == 0)
                {
                    Error("AUISEL017", $":{name}() requires at least one valid selector.", opening);
                    pseudo = null!;
                    specificity = default;
                    dependencies = default;
                    return false;
                }

                var kind = name switch
                {
                    "is" => PseudoSelectorKind.Is,
                    "where" => PseudoSelectorKind.Where,
                    "not" => PseudoSelectorKind.Not,
                    _ => PseudoSelectorKind.Has,
                };
                pseudo = new PseudoSelector(kind, StylePseudoState.None, default, nested);
                specificity = kind == PseudoSelectorKind.Where
                    ? Specificity.Zero
                    : Specificity.Max(nested.Select(static item => item.Specificity));
                dependencies = nested.Aggregate(
                    SelectorDependency.None,
                    static (current, item) => current | item.Dependencies);
                if (kind == PseudoSelectorKind.Has)
                {
                    dependencies |= SelectorDependency.Descendant;
                }

                return true;
            }

            Error("AUISEL018", $"Unsupported functional pseudo selector ':{name}()'.", opening);
            pseudo = null!;
            specificity = default;
            dependencies = default;
            return false;
        }

        private bool TryReadAttributeOperator(out AttributeMatchOperator operation)
        {
            operation = AttributeMatchOperator.Equals;
            if (_index >= _source.Length)
            {
                return false;
            }

            if (_source[_index] == '=')
            {
                _index++;
                return true;
            }

            if (_index + 1 >= _source.Length || _source[_index + 1] != '=')
            {
                return false;
            }

            operation = _source[_index] switch
            {
                '~' => AttributeMatchOperator.Includes,
                '|' => AttributeMatchOperator.DashMatch,
                '^' => AttributeMatchOperator.Prefix,
                '$' => AttributeMatchOperator.Suffix,
                '*' => AttributeMatchOperator.Contains,
                _ => (AttributeMatchOperator)byte.MaxValue,
            };
            if (operation == (AttributeMatchOperator)byte.MaxValue)
            {
                return false;
            }

            _index += 2;
            return true;
        }

        private string? ReadAttributeValue()
        {
            if (_index >= _source.Length)
            {
                return null;
            }

            if (_source[_index] is '\'' or '"')
            {
                var quote = _source[_index++];
                var start = _index;
                var escaped = false;
                while (_index < _source.Length)
                {
                    var current = _source[_index];
                    if (escaped)
                    {
                        escaped = false;
                    }
                    else if (current == '\\')
                    {
                        escaped = true;
                    }
                    else if (current == quote)
                    {
                        var value = TextSplitter.Unescape(_source[start.._index]);
                        _index++;
                        return value;
                    }

                    _index++;
                }

                return null;
            }

            var valueStart = _index;
            while (_index < _source.Length && !char.IsWhiteSpace(_source[_index]) && _source[_index] != ']')
            {
                _index++;
            }

            return valueStart == _index ? null : TextSplitter.Unescape(_source[valueStart.._index]);
        }

        private bool TryReadBalancedBody(out string body)
        {
            var opening = _index++;
            var start = _index;
            var depth = 1;
            var quote = '\0';
            var escaped = false;
            while (_index < _source.Length)
            {
                var current = _source[_index];
                if (quote != '\0')
                {
                    if (escaped)
                    {
                        escaped = false;
                    }
                    else if (current == '\\')
                    {
                        escaped = true;
                    }
                    else if (current == quote)
                    {
                        quote = '\0';
                    }
                }
                else if (current is '\'' or '"')
                {
                    quote = current;
                }
                else if (current == '(')
                {
                    depth++;
                }
                else if (current == ')' && --depth == 0)
                {
                    body = _source[start.._index].Trim();
                    _index++;
                    return true;
                }

                _index++;
            }

            _index = opening;
            body = string.Empty;
            return false;
        }

        private string ReadIdentifier()
        {
            var start = _index;
            while (_index < _source.Length && IsIdentifierPart(_source[_index]))
            {
                _index++;
            }

            return start == _index ? string.Empty : TextSplitter.Unescape(_source[start.._index]);
        }

        private bool SkipWhitespace()
        {
            var start = _index;
            while (_index < _source.Length && char.IsWhiteSpace(_source[_index]))
            {
                _index++;
            }

            return _index > start;
        }

        private bool InvalidPseudo(
            string name,
            int offset,
            out PseudoSelector pseudo,
            out Specificity specificity,
            out SelectorDependency dependencies)
        {
            Error("AUISEL019", $":{name} does not accept arguments.", offset);
            pseudo = null!;
            specificity = default;
            dependencies = default;
            return false;
        }

        private void Error(string code, string message, int offset) =>
            _diagnostics.Error(code, message, _baseOffset + Math.Clamp(offset, 0, _source.Length));

        private static bool TryMapState(string name, out StylePseudoState required, out StylePseudoState forbidden)
        {
            required = name switch
            {
                "hover" => StylePseudoState.Hover,
                "active" => StylePseudoState.Active,
                "focus" => StylePseudoState.Focus,
                "focus-within" => StylePseudoState.FocusWithin,
                "disabled" => StylePseudoState.Disabled,
                "checked" => StylePseudoState.Checked,
                "invalid" => StylePseudoState.Invalid,
                "read-only" => StylePseudoState.ReadOnly,
                "selected" => StylePseudoState.Selected,
                "expanded" => StylePseudoState.Expanded,
                _ => StylePseudoState.None,
            };

            forbidden = name switch
            {
                "enabled" => StylePseudoState.Disabled,
                "read-write" => StylePseudoState.ReadOnly,
                _ => StylePseudoState.None,
            };

            return required != StylePseudoState.None || forbidden != StylePseudoState.None;
        }

        private static bool TryParseNth(string source, out NthExpression expression)
        {
            var normalized = new string(source.Where(static character => !char.IsWhiteSpace(character)).ToArray())
                .ToLowerInvariant();
            if (normalized == "odd")
            {
                expression = new NthExpression(2, 1);
                return true;
            }

            if (normalized == "even")
            {
                expression = new NthExpression(2, 0);
                return true;
            }

            var nIndex = normalized.IndexOf('n');
            if (nIndex < 0)
            {
                if (int.TryParse(normalized, out var index))
                {
                    expression = new NthExpression(0, index);
                    return true;
                }

                expression = default;
                return false;
            }

            var stepText = normalized[..nIndex];
            var offsetText = normalized[(nIndex + 1)..];
            int step;
            if (stepText is "" or "+")
            {
                step = 1;
            }
            else if (stepText == "-")
            {
                step = -1;
            }
            else if (!int.TryParse(stepText, out step))
            {
                expression = default;
                return false;
            }

            var offset = 0;
            if (offsetText.Length > 0 && !int.TryParse(offsetText, out offset))
            {
                expression = default;
                return false;
            }

            expression = new NthExpression(step, offset);
            return true;
        }

        private static bool IsIdentifierStart(char value) =>
            char.IsLetter(value) || value is '_' or '-' || value >= 128;

        private static bool IsIdentifierPart(char value) =>
            char.IsLetterOrDigit(value) || value is '_' or '-' or '\\' || value >= 128;
    }
}
