/*
 * This file is part of the Code::Blocks IDE and licensed under the GNU General Public License, version 3
 * http://www.gnu.org/licenses/gpl-3.0.html
 *
 * $Revision: 5434 $
 * $Id: tokenizer.cpp 5434 2009-02-02 18:59:06Z killerbot $
 * $HeadURL: http://svn.berlios.de/svnroot/repos/codeblocks/trunk/src/plugins/codecompletion/parser/tokenizer.cpp $
 */
#include "stdafx.h"
#include "tokenizer.h"
/*
#include <sdk.h>
#include "tokenizer.h"
#include <wx/utils.h>
#include <wx/file.h>
#include <wx/msgdlg.h>
#include "manager.h"
#include <cctype>
#include <globals.h>
*/

namespace TokenizerConsts
{
const wxString colon(_T(":"));
const wxString colon_colon(_T("::"));
const wxString operator_str(_T("operator"));
const wxString include_str(_T("#include"));
const wxString if_str(_T("#if"));
const wxString hash(_T("#"));
const wxString tabcrlf(_T("\t\n\r"));
};

// static
ConfigManagerContainer::StringToStringMap Tokenizer::s_Replacements;

Tokenizer::Tokenizer(const wxString& filename)
    : m_Filename(filename),
    m_peek(_T("")),
    m_curtoken(_T("")),
    m_peekavailable(false),
    m_BufferLen(0),
    m_NestLevel(0),
    m_UndoNestLevel(0),
    m_TokenIndex(0),
    m_UndoTokenIndex(0),
    m_LineNumber(1),
    m_UndoLineNumber(1),
    m_PeekTokenIndex(0),
    m_PeekLineNumber(0),
    m_PeekNestLevel(0),
    m_SavedNestingLevel(0),
    m_IsOK(false),
    m_IsOperator(false),
    m_LastWasPreprocessor(false),
    m_SkipUnwantedTokens(true),
    m_pLoader(0)
{
    //ctor
    m_Options.wantPreprocessor = false;
    if (!m_Filename.IsEmpty())
        Init(m_Filename);
}

Tokenizer::~Tokenizer()
{
    //dtor
}

bool Tokenizer::Init(const wxString& filename, LoaderBase* loader)
{
    m_pLoader = loader;
    BaseInit();
    if (filename.IsEmpty())
    {
        if (m_Filename.IsEmpty())
        {
//            cbMessageBox(_T("Tokenizer::Init() called without filename..."));
            return false;
        }
    }
    else
        m_Filename = filename;

    if (!wxFileExists(m_Filename))
        return false;

    if (!ReadFile())
    {
//        cbMessageBox(_T("File ") + filename + _T(" does not exist..."));
        return false;
    }

    if (!m_BufferLen)
    {
        //cbMessageBox("File is empty!");
        return false;
    }

    m_IsOK = true;
    return true;
}

bool Tokenizer::InitFromBuffer(const wxString& buffer)
{
    BaseInit();
    m_Buffer = buffer;
    m_BufferLen = buffer.Length();
    m_IsOK = true;
    m_Filename.Clear();
    return true;
}

void Tokenizer::BaseInit()
{
    m_TokenIndex = 0;
    m_UndoTokenIndex = 0;
    m_LineNumber = 1;
    m_UndoLineNumber = 1;
    m_PeekTokenIndex = 0;
    m_PeekLineNumber = 0;
    m_PeekNestLevel = 0;
    m_NestLevel = 0;
    m_SavedNestingLevel = 0;
    m_UndoNestLevel = 0;
    m_IsOperator = false;
    m_BufferLen = 0;
    m_LastWasPreprocessor = false;
    m_LastPreprocessor.Clear();
    m_Buffer.Clear();
    m_IsOK = false;
}

bool Tokenizer::ReadFile()
{
    if (m_pLoader)
    {
        char* data = m_pLoader->GetData();
        m_BufferLen = m_pLoader->GetLength();

        // the following code is faster than DetectEncodingAndConvert()
//        DetectEncodingAndConvert(data, m_Buffer);

        // same code as in cbC2U() but with the addition of the string length (3rd param in unicode version)
        // and the fallback encoding conversion
//#if wxUSE_UNICODE
#if _UNICODE
		/*
        m_Buffer = wxString(data, wxConvUTF8, m_BufferLen);
        if (m_Buffer.Length() == 0)
        {
            // could not read as utf-8 encoding, try iso8859-1
            m_Buffer = wxString(data, wxConvISO8859_1, m_BufferLen);
        }
		*/
		m_BufferLen = cbC2U(data,m_BufferLen,&m_Buffer);
		m_BufferLen = m_Buffer.Length();
#else
        m_Buffer = wxString(data, m_BufferLen);
#endif

        if (m_BufferLen != m_Buffer.Length())
        {
            // inconsistency!
            // correct it to avoid crashes but this file will probably NOT be parsed correctly
            m_BufferLen = m_Buffer.Length();
//            asm("int $3;");
        }
        return data != 0;
    };

    if (!wxFileExists(m_Filename))
        return false;

    // open file
    wxFile file(m_Filename);

    if (!cbRead(file,m_Buffer))
        return false;
    m_BufferLen = m_Buffer.Length();

    return true;
}

bool Tokenizer::SkipWhiteSpace()
{
    // skip spaces, tabs, etc.
    while (CurrentChar() <= _T(' ') && MoveToNextChar()) // don't check EOF when MoveToNextChar already does, also replace isspace() which calls msvcrt.dll
        ;                                                // with a dirty hack:  CurrentChar() <= ' ' is "good enough" here
    if (IsEOF())
        return false;
    return true;
}

bool Tokenizer::SkipToChar(const wxChar& ch)
{
    // skip everything until we find ch
    while(true)
    {
        while (CurrentChar() != ch && MoveToNextChar())  // don't check EOF when MoveToNextChar already does
            ;

        if (IsEOF())
            return false;

        if (PreviousChar() != '\\')
            break;
        else
        {
            // check for "\\"
            if (m_TokenIndex - 2 >= 0 && m_Buffer.GetChar(m_TokenIndex - 2) == '\\')
                break;
        }
        MoveToNextChar();
    }
    return true;
}


bool Tokenizer::SkipToOneOfChars(const wxChar* chars, bool supportNesting)
{
    // skip everything until we find any one of chars
    while (1)
    {
        while (NotEOF() && !CharInString(CurrentChar(), chars))
        {
            if (CurrentChar() == '"' || CurrentChar() == '\'')
            {
                // this is the case that match is inside a string!
                wxChar ch = CurrentChar();
                MoveToNextChar();
                SkipToChar(ch);
            }
            MoveToNextChar();

            // make sure we skip comments
            if (CurrentChar() == '/')
                SkipComment(); // this will decide if it is a comment

            // use 'while' here to cater for consecutive blocks to skip (e.g. sometemplate<foo>(bar)
            // must skip <foo> and immediately after (bar))
            // because if we don't, the next block won't be skipped ((bar) in the example) leading to weird
            // parsing results
            bool done = false;
            while (supportNesting && !done)
            {
                switch (CurrentChar())
                {
                    case '{': SkipBlock('{'); break;
                    case '(': SkipBlock('('); break;
                    case '[': SkipBlock('['); break;
                    case '<': // don't skip if << operator
                        if (NextChar() == '<')
                            MoveToNextChar(2); // skip it and also the next '<' or the next '<' leads to a SkipBlock('<');
                        else
                            SkipBlock('<');
                        break;
                    default: done = true; break;
                }
            }
        }
        if (PreviousChar() != '\\')
            break;
        else
        {
            // check for "\\"
            if (m_TokenIndex - 2 >= 0 && m_Buffer.GetChar(m_TokenIndex - 2) == '\\')
                break;
        }
        MoveToNextChar();
    }
    if (IsEOF())
        return false;
    return true;
}

wxString Tokenizer::ReadToEOL(bool nestBraces)
{
    unsigned int idx = m_TokenIndex;
    SkipToEOL(nestBraces);
    return m_Buffer.Mid(idx, m_TokenIndex - idx);
}

bool Tokenizer::SkipToEOL(bool nestBraces, bool skippingComment)
{
    // skip everything until we find EOL
    while (1)
    {
        while (NotEOF() && CurrentChar() != '\n')
        {
            if (CurrentChar() == '/' && NextChar() == '*')
            {
                SkipComment(false); // don't skip whitespace after the comment
                if (skippingComment && CurrentChar() == '\n')
                {
                    continue; // early exit from the loop
                }
            }
            if (nestBraces && CurrentChar() == _T('{'))
                ++m_NestLevel;
            else if (nestBraces && CurrentChar() == _T('}'))
                --m_NestLevel;
            MoveToNextChar();
        }
        wxChar last = PreviousChar();
        // if DOS line endings, we 've hit \r and we skip to \n...
        if (last == '\r')
            last = m_Buffer.GetChar(m_TokenIndex - 2);
        if (IsEOF() || last != '\\')
            break;
        else
            MoveToNextChar();
    }
    if (IsEOF())
        return false;
    return true;
}

bool Tokenizer::SkipBlock(const wxChar& ch)
{
    // skip blocks () [] {} <>
    wxChar match;
    switch (ch)
    {
        case '(': match = ')'; break;
        case '[': match = ']'; break;
        case '{': match = '}'; break;
        case '<': match = '>'; break;
        default : return false;
    }

    MoveToNextChar();
    int count = 1; // counter for nested blocks (xxx())
    while (NotEOF())
    {
        bool noMove = false;
        if (CurrentChar() == '/')
            SkipComment(); // this will decide if it is a comment

        if (CurrentChar() == '"' || CurrentChar() == '\'')
        {
            // this is the case that match is inside a string!
            wxChar ch = CurrentChar();
            MoveToNextChar();
            SkipToChar(ch);
            MoveToNextChar();
            // don't move to next char below if concatenating strings (e.g. printf("" ""))
            if (CurrentChar() == '"' || CurrentChar() == '\'')
                noMove = true;
        }
        if (CurrentChar() == ch)
            ++count;
        else if (CurrentChar() == match)
            --count;
        if (!noMove)
            MoveToNextChar();
        if (count == 0)
            break;
    }
    if (IsEOF())
        return false;
    return true;
}

bool Tokenizer::SkipComment(bool skipWhiteAtEnd) // = true
{
    // C/C++ style comments
    bool is_comment = CurrentChar() == '/' && (NextChar() == '/' || NextChar() == '*');
    if (!is_comment)
        return true;

    bool cstyle = NextChar() == '*';
    MoveToNextChar(2);
    while (1)
    {
        if (!cstyle)
        {
            if (!SkipToEOL(false, true))
                return false;
            MoveToNextChar();
            break;
        }
        else
        {
            if (SkipToChar('/'))
            {
                if (PreviousChar() == '*')
                {
                    MoveToNextChar();
                    break;
                }
                MoveToNextChar();
            }
            else
                return false;
        }
    }
    if (IsEOF())
        return false;
    if (skipWhiteAtEnd && !SkipWhiteSpace())
        return false;
    return CurrentChar() == '/' ? SkipComment() : true; // handle chained comments
}

//vfc add bGetValue
bool Tokenizer::SkipUnwanted(bool bGetValue) 
{
    while (CurrentChar() == '#' ||
            (!m_IsOperator && CurrentChar() == '=') ||
            (!m_IsOperator && CurrentChar() == '[') ||
            CurrentChar() == '?' ||
            (CurrentChar() == '/' && (NextChar() == '/' || NextChar() == '*') ))
    {
        bool skipPreprocessor = false; // used for #include
        while (m_Buffer.Mid(m_TokenIndex, 2) == _T("//") ||
                m_Buffer.Mid(m_TokenIndex, 2) == _T("/*"))
        {
            // C/C++ style comments
            SkipComment();
            if (IsEOF())
                return false;
            if (!SkipWhiteSpace())
                return false;
        }

        while (CurrentChar() == '#')
        {
            // preprocessor directives
            // we only care for #include and #define, for now
            unsigned int backupIdx = m_TokenIndex;
            MoveToNextChar();
            SkipWhiteSpace();
            if ((CurrentChar() == 'i' && NextChar() == 'n') || // in(clude)
                (CurrentChar() == 'i' && NextChar() == 'f') || // if(|def|ndef)
                (CurrentChar() == 'e' && NextChar() == 'l') || // el(se|if)
                (CurrentChar() == 'e' && NextChar() == 'n') || // en(dif)
                (m_Options.wantPreprocessor && CurrentChar() == 'd' && NextChar() == 'e')) // de(fine)
            {
                // ok, we have something like #in(clude)
                m_LastWasPreprocessor = true;
                m_LastPreprocessor.Clear();
                m_TokenIndex = backupIdx; // keep #
                skipPreprocessor = true;
                break;
            }
            else
            {
                // skip the rest for now...
                SkipToEOL(false);
                if (!SkipWhiteSpace())
                    return false;
            }
            if (skipPreprocessor)
                break;
        }

        while (CurrentChar() == '[')
        {
            // array subscripts
            // skip them for now...
            SkipBlock('[');
            if (!SkipWhiteSpace())
                return false;
        }

        while (CurrentChar() == '=')
        {
            // skip assignments
            // TODO: what happens with operators?
			if (bGetValue == true)
			{
				MoveToNextChar();
				SkipWhiteSpace();
				return true;
			}
	        else if (!SkipToOneOfChars(_T(",;}"), true))
                return false;
        }

        while (CurrentChar() == '?')
        {
            // skip "condition ? true : false"
            // TODO: what happens with operators?
            if (!SkipToOneOfChars(_T(";}")))
                return false;
        }
        if (skipPreprocessor)
            break;
    }
    return true;
}

//vfc add bGetValue
wxString Tokenizer::GetToken(bool bGetValue, bool bTemplate)
{
    m_UndoTokenIndex = m_TokenIndex;
    m_UndoLineNumber = m_LineNumber;
    m_UndoNestLevel  = m_NestLevel;

	if (bGetValue)
	{
        m_curtoken = DoGetToken(bGetValue, bTemplate);
	}
    else if(m_peekavailable)
    {
        m_TokenIndex = m_PeekTokenIndex;
        m_LineNumber = m_PeekLineNumber;
        m_NestLevel  = m_PeekNestLevel;
        m_curtoken   = m_peek;
    }
    else
        m_curtoken = DoGetToken(bGetValue, bTemplate);

    m_peekavailable = false;

    return ThisOrReplacement(m_curtoken);
}

wxString Tokenizer::PeekToken(bool bGetValue, bool bTemplate)
{
    if(!m_peekavailable)
    {
        m_peekavailable = true;
        unsigned int undoTokenIndex = m_TokenIndex;
        unsigned int undoLineNumber = m_LineNumber;
        unsigned int undoNestLevel = m_NestLevel;
        m_peek = DoGetToken(bGetValue,bTemplate);
        m_PeekTokenIndex = m_TokenIndex;
        m_PeekLineNumber = m_LineNumber;
        m_PeekNestLevel = m_NestLevel;
        m_TokenIndex = undoTokenIndex;
        m_LineNumber = undoLineNumber;
        m_NestLevel = undoNestLevel;
    }
    return m_peek;
}

void Tokenizer::UngetToken()
{
    m_PeekTokenIndex = m_TokenIndex;
    m_PeekLineNumber = m_LineNumber;
    m_PeekNestLevel = m_NestLevel;
    m_TokenIndex = m_UndoTokenIndex;
    m_LineNumber = m_UndoLineNumber;
    m_NestLevel = m_UndoNestLevel;
    m_peek = m_curtoken;
    m_peekavailable = true;
}

//vfc add bGetValue
wxString Tokenizer::DoGetToken(bool bGetValue, bool bTemplate)
{
    if (IsEOF())
        return wxEmptyString;

    if (!SkipWhiteSpace())
        return wxEmptyString;

    if (m_SkipUnwantedTokens && !SkipUnwanted(bGetValue))
        return wxEmptyString;

    // if m_SkipUnwantedTokens is false, we need to handle comments here too
    if (!m_SkipUnwantedTokens)
        SkipComment();

    int start = m_TokenIndex;
    wxString m_Str;
    wxChar c = CurrentChar();

    if (c == '_' || wxIsalpha(c))
    {
        // keywords, identifiers, etc.

        // operator== is cheaper than wxIsalnum, also MoveToNextChar already includes IsEOF
        while (  ( CurrentChar() == '_' ||
                   wxIsalnum(CurrentChar()) ) && MoveToNextChar()  )
        ;

        if (IsEOF())
            return wxEmptyString;
        m_Str = m_Buffer.Mid(start, m_TokenIndex - start);
        m_IsOperator = m_Str.IsSameAs(TokenizerConsts::operator_str);
    }
#ifdef __WXMSW__ // This is a Windows only bug!
    else if (c == 178  || c == 179 || c == 185) // fetch ?and ?
    {
        m_Str = c;
        MoveToNextChar();
    }
#endif
    else if (wxIsdigit(CurrentChar()))
    {
        // numbers
        while (NotEOF() && CharInString(CurrentChar(), _T("0123456789.abcdefABCDEFXxLl")))
            MoveToNextChar();
        if (IsEOF())
            return wxEmptyString;
        m_Str = m_Buffer.Mid(start, m_TokenIndex - start);
        m_IsOperator = false;
    }
    else if (CurrentChar() == '"' ||
            CurrentChar() == '\'')
    {
        // string, char, etc.
        wxChar match = CurrentChar();
        MoveToNextChar();  // skip starting ' or "
        if (!SkipToChar(match))
            return wxEmptyString;
        MoveToNextChar(); // skip ending ' or "
        m_Str = m_Buffer.Mid(start, m_TokenIndex - start);
    }
    else if (CurrentChar() == ':')
    {
        if (NextChar() == ':')
        {
            MoveToNextChar();
            MoveToNextChar();
            m_Str.assign(TokenizerConsts::colon_colon); // this only copies a pointer, but operator= allocates memory and does a memcpy!
        }
        else
        {
            MoveToNextChar();
            m_Str.assign(TokenizerConsts::colon);
        }
    }
	else if (CurrentChar() == '<' && bTemplate)
	{
		wxChar match = _T('>');
		MoveToNextChar();
		if (!SkipToOneOfChars(_T(">\r\n")),false)
			return wxEmptyString;
		MoveToNextChar();
		wxString tmp = m_Buffer.Mid(start+1,m_TokenIndex-start-2);
		tmp.Trim();
        m_Str = _T("<");
		m_Str += tmp;
		m_Str += _T(">");//m_Buffer.Mid(start, m_TokenIndex - start);
	}
    else if (CurrentChar() == '(')
    {
        m_IsOperator = false;
        // skip blocks () []
        if (!SkipBlock(CurrentChar()))
            return wxEmptyString;
        wxString tmp = m_Buffer.Mid(start, m_TokenIndex - start);
//        tmp.Replace(_T("\t"), _T(" ")); // replace tabs with spaces
//        tmp.Replace(_T("\n"), _T(" ")); // replace LF with spaces
//        tmp.Replace(_T("\r"), _T(" ")); // replace CR with spaces
        { // this is much faster:
            size_t i;
            while((i = tmp.find_first_of(TokenizerConsts::tabcrlf)) != wxString::npos)
                //tmp[i] = _T(' ');
				tmp.SetAt(i,_T(' '));
        }
        // fix-up arguments (remove excessive spaces/tabs/newlines)
        for (unsigned int i = 0; i < tmp.Length() - 1; ++i)
        {
            //skip spaces before '=' and ','
            if (tmp.GetChar(i) == ' ' && (tmp.GetChar(i + 1) == ',' || tmp.GetChar(i + 1) == '='))
				continue;

            if (tmp.GetChar(i) == '/' && tmp.GetChar(i + 1) == '*')
            {
                // skip C comments
                i += 2;
                while (i < tmp.Length() - 1)
                {
                    if (tmp.GetChar(i) == '*' && tmp.GetChar(i + 1) == '/')
                        break;
                    ++i;
                }
                if (i >= tmp.Length() - 1 || tmp.GetChar(i + 1) != '/')
                    continue; // we failed...
                i += 2;
            }
            else if (tmp.GetChar(i) == '=')
            {
                // skip default assignments
                ++i;
                int level = 0; // nesting parenthesis
                while (i < tmp.Length())
                {
                    if (tmp.GetChar(i) == '(')
                        ++level;
                    else if (tmp.GetChar(i) == ')')
                        --level;
                    if ((tmp.GetChar(i) == ',' && level == 0) ||
                        (tmp.GetChar(i) == ')' && level < 0))
                        break;
                    ++i;
                }
                if (i < tmp.Length() && tmp.GetChar(i) == ',')
                    --i;
                continue; // we are done here
            }

            if (i < tmp.Length() - 1)
            {
                if ((tmp.GetChar(i)     == ' ') && (tmp.GetChar(i + 1) == ' '))
                    continue; // skip excessive spaces

                // in case of c-style comments "i" might already be tmp.Length()
                // thus do only add the current char otherwise.
                // otherwise the following statement:
                // m_Str << _T(')');
                // below would add another closing bracket.
                m_Str << tmp.GetChar(i);
            }
        }
        m_Str << _T(')'); // add closing parenthesis (see "i < tmp.Length() - 1" in previous "for")
//        m_Str.Replace(_T("  "), _T(" ")); // replace two-spaces with single-space (introduced if it skipped comments or assignments)
//        m_Str.Replace(_T("( "), _T("("));
//        m_Str.Replace(_T(" )"), _T(")"));
        //Str.Replace is massive overkill here since it has to allocate one new block per replacement
        CompactSpaces(m_Str);
    }
    else
    {
        if (CurrentChar() == '{')
            ++m_NestLevel;
        else if (CurrentChar() == '}')
            --m_NestLevel;
        m_Str = CurrentChar();
        MoveToNextChar();
    }

    if (m_LastWasPreprocessor && !m_Str.IsSameAs(_T("#")) && !m_LastPreprocessor.IsSameAs(_T("#")))
    {
        if (!m_LastPreprocessor.IsSameAs(TokenizerConsts::include_str))
        {
            // except for #include and #if[[n]def], all other preprocessor directives need only
            // one word exactly after the directive, e.g. #define THIS_WORD
            SkipToEOL();
        }
        m_LastPreprocessor.Clear();
    }

    if (m_LastWasPreprocessor)
        m_LastPreprocessor << m_Str;
    m_LastWasPreprocessor = false;

    return m_Str;
}
