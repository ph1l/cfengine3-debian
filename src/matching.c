/* 
   Copyright (C) Cfengine AS

   This file is part of Cfengine 3 - written and maintained by Cfengine AS.
 
   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; version 3.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
 
  You should have received a copy of the GNU General Public License  
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA

  To the extent this program is licensed as part of the Enterprise
  versions of Cfengine, the applicable Commerical Open Source License
  (COSL) may apply to this file if you as a licensee so wish it. See
  included file COSL.txt.

*/

/*****************************************************************************/
/*                                                                           */
/* File: matching.c                                                          */
/*                                                                           */
/*****************************************************************************/

#include "cf3.defs.h"
#include "cf3.extern.h"

#include "vars.h"

#ifdef HAVE_PCRE_H
# include <pcre.h>
#endif

#ifdef HAVE_PCRE_PCRE_H
# include <pcre/pcre.h>
#endif

/*********************************************************************/
/* Wrappers                                                          */
/*********************************************************************/

static pcre *CompileRegExp(const char *regexp)
{
    pcre *rx;
    const char *errorstr;
    int erroffset;

    rx = pcre_compile(regexp, PCRE_MULTILINE | PCRE_DOTALL, &errorstr, &erroffset, NULL);

    if (rx == NULL)
    {
        CfOut(cf_error, "", "Regular expression error \"%s\" in expression \"%s\" at %d\n", errorstr, regexp,
              erroffset);
    }

    return rx;
}

/*********************************************************************/

static int RegExMatchSubString(pcre *rx, const char *teststring, int *start, int *end)
{
    int ovector[OVECCOUNT], i, rc;

    if ((rc = pcre_exec(rx, NULL, teststring, strlen(teststring), 0, 0, ovector, OVECCOUNT)) >= 0)
    {
        *start = ovector[0];
        *end = ovector[1];

        DeleteScope("match");
        NewScope("match");

        for (i = 0; i < rc; i++)        /* make backref vars $(1),$(2) etc */
        {
            char lval[4];
            const char *backref_start = teststring + ovector[i * 2];
            int backref_len = ovector[i * 2 + 1] - ovector[i * 2];

            if (backref_len < CF_MAXVARSIZE)
            {
                char substring[CF_MAXVARSIZE];

                strlcpy(substring, backref_start, MIN(CF_MAXVARSIZE, backref_len + 1));
                snprintf(lval, 3, "%d", i);
                ForceScalar(lval, substring);
            }
        }
    }
    else
    {
        *start = 0;
        *end = 0;
    }

    free(rx);
    return rc >= 0;
}

/*********************************************************************/

static int RegExMatchFullString(pcre *rx, const char *teststring)
{
    int match_start;
    int match_len;

    if (RegExMatchSubString(rx, teststring, &match_start, &match_len))
    {
        return match_start == 0 && match_len == strlen(teststring);
    }
    else
    {
        return false;
    }
}

/*********************************************************************/

static char *FirstBackReference(pcre *rx, const char *teststring)
{
    static char backreference[CF_BUFSIZE];

    int ovector[OVECCOUNT], i, rc;

    memset(backreference, 0, CF_BUFSIZE);

    if ((rc = pcre_exec(rx, NULL, teststring, strlen(teststring), 0, 0, ovector, OVECCOUNT)) >= 0)
    {
        for (i = 1; i < rc; i++)        /* make backref vars $(1),$(2) etc */
        {
            const char *backref_start = teststring + ovector[i * 2];
            int backref_len = ovector[i * 2 + 1] - ovector[i * 2];

            if (backref_len < CF_MAXVARSIZE)
            {
                strncpy(backreference, backref_start, backref_len);
            }

            break;
        }
    }

    free(rx);

    return backreference;
}

bool ValidateRegEx(const char *regex)
{
    pcre *rx = CompileRegExp(regex);
    bool regex_valid = rx != NULL;

    free(rx);
    return regex_valid;
}

/*************************************************************************/
/* WILDCARD TOOLKIT : Level 0                                            */
/*************************************************************************/

int FullTextMatch(const char *regexp, const char *teststring)
{
    pcre *rx;

    if (strcmp(regexp, teststring) == 0)
    {
        return true;
    }

    rx = CompileRegExp(regexp);

    if (rx == NULL)
    {
        return false;
    }

    if (RegExMatchFullString(rx, teststring))
    {
        return true;
    }
    else
    {
        return false;
    }
}

/*************************************************************************/

char *ExtractFirstReference(const char *regexp, const char *teststring)
{
    static char *nothing = "";
    char *backreference;

    pcre *rx;

    if (regexp == NULL || teststring == NULL)
    {
        return nothing;
    }

    rx = CompileRegExp(regexp);

    if (rx == NULL)
    {
        return nothing;
    }

    backreference = FirstBackReference(rx, teststring);

    if (strlen(backreference) == 0)
    {
        CfDebug("The regular expression \"%s\" yielded no matching back-reference\n", regexp);
        strncpy(backreference, "CF_NOMATCH", CF_MAXVARSIZE);
    }
    else
    {
        CfDebug("The regular expression \"%s\" yielded backreference \"%s\" on %s\n", regexp, backreference,
                teststring);
    }

    return backreference;
}

/*************************************************************************/

int BlockTextMatch(char *regexp, char *teststring, int *start, int *end)
{
    pcre *rx = CompileRegExp(regexp);

    if (rx == NULL)
    {
        return 0;
    }

    if (RegExMatchSubString(rx, teststring, start, end))
    {
        return true;
    }
    else
    {
        return false;
    }
}

/*********************************************************************/

int IsRegex(char *str)
{
    char *sp;
    int ret = false;
    enum
    { r_norm, r_norepeat, r_literal } special = r_norepeat;
    int bracket = 0;
    int paren = 0;

/* Try to see when something is intended as a regular expression */

    for (sp = str; *sp != '\0'; sp++)
    {
        if (special == r_literal)
        {
            special = r_norm;
            continue;
        }
        else if (*sp == '\\')
        {
            special = r_literal;
            continue;
        }
        else if (bracket && *sp != ']')
        {
            if (*sp == '[')
            {
                return false;
            }
            continue;
        }

        switch (*sp)
        {
        case '^':
            special = (sp == str) ? r_norepeat : r_norm;
            break;
        case '*':
        case '+':
            if (special == r_norepeat)
            {
                return false;
            }
            special = r_norepeat;
            ret = true;
            break;
        case '[':
            special = r_norm;
            bracket++;
            ret = true;
            break;
        case ']':
            if (bracket == 0)
            {
                return false;
            }
            bracket = 0;
            special = r_norm;
            break;
        case '(':
            special = r_norepeat;
            paren++;
            break;

        case ')':
            special = r_norm;
            paren--;
            if (paren < 0)
            {
                return false;
            }
            break;

        case '|':
            special = r_norepeat;
            if (paren > 0)
            {
                ret = true;
            }
            break;

        default:
            special = r_norm;
        }

    }

    if (bracket != 0 || paren != 0 || special == r_literal)
    {
        return false;
    }
    else
    {
        return ret;
    }
}

/*********************************************************************/

int IsPathRegex(char *str)
{
    char *sp;
    int result = false, s = 0, r = 0;

    if ((result = IsRegex(str)))
    {
        for (sp = str; *sp != '\0'; sp++)
        {
            switch (*sp)
            {
            case '[':
                s++;
                break;
            case ']':
                s--;
                if (s % 2 == 0)
                {
                    result++;
                }
                break;
            case '(':
                r++;
                break;
            case ')':
                r--;
                if (r % 2 == 0)
                {
                    result++;
                }
                break;
            default:

                if (*sp == FILE_SEPARATOR && (r || s))
                {
                    CfOut(cf_error, "",
                          "Path regular expression %s seems to use expressions containing the directory symbol %c", str,
                          FILE_SEPARATOR);
                    CfOut(cf_error, "", "Use a work-around to avoid pathological behaviour\n");
                    return false;
                }
                break;
            }
        }
    }

    return result;
}

/*********************************************************************/

int IsRegexItemIn(Item *list, char *regex)
   /* Checks whether item matches a list of wildcards */
{
    Item *ptr;

    for (ptr = list; ptr != NULL; ptr = ptr->next)
    {
        if (ptr->classes && IsExcluded(ptr->classes))
        {
            continue;
        }

        /* Avoid using regex if possible, due to memory leak */

        if (strcmp(regex, ptr->name) == 0)
        {
            return (true);
        }

        /* Make it commutative */

        if (FullTextMatch(regex, ptr->name) || FullTextMatch(ptr->name, regex))
        {
            CfDebug("IsRegexItem(%s,%s)\n", regex, ptr->name);
            return (true);
        }
    }

    return (false);
}

/*********************************************************************/

int MatchPolicy(char *camel, char *haystack, Attributes a, Promise *pp)
{
    Rlist *rp;
    char *sp, *spto, *firstchar, *lastchar;
    enum insert_match opt;
    char work[CF_BUFSIZE], final[CF_BUFSIZE];
    Item *list = SplitString(camel, '\n'), *ip;
    int direct_cmp = false, ok = false, escaped = false;

//Split into separate lines first

    for (ip = list; ip != NULL; ip = ip->next)
    {
        ok = false;
        direct_cmp = (strcmp(camel, haystack) == 0);

        if (a.insert_match == NULL)
        {
            // No whitespace policy means exact_match
            ok = ok || direct_cmp;
            break;
        }

        memset(final, 0, CF_BUFSIZE);
        strncpy(final, ip->name, CF_BUFSIZE - 1);

        for (rp = a.insert_match; rp != NULL; rp = rp->next)
        {
            opt = String2InsertMatch(rp->item);

            /* Exact match can be done immediately */

            if (opt == cf_exact_match)
            {
                if (rp->next != NULL || rp != a.insert_match)
                {
                    CfOut(cf_error, "", " !! Multiple policies conflict with \"exact_match\", using exact match");
                    PromiseRef(cf_error, pp);
                }

                ok = ok || direct_cmp;
                break;
            }

            if (!escaped)
            {    
            // Need to escape the original string once here in case it contains regex chars when non-exact match
            EscapeRegexChars(ip->name, final, CF_BUFSIZE - 1);
            escaped = true;
            }
            
            if (opt == cf_ignore_embedded)
            {
                memset(work, 0, CF_BUFSIZE);

                // Strip initial and final first

                for (firstchar = final; isspace(*firstchar); firstchar++)
                {
                }

                for (lastchar = final + strlen(final) - 1; lastchar > firstchar && isspace(*lastchar); lastchar--)
                {
                }

                for (sp = final, spto = work; *sp != '\0'; sp++)
                {
                    if (sp > firstchar && sp < lastchar)
                    {
                        if (isspace(*sp))
                        {
                            while (isspace(*(sp + 1)))
                            {
                                sp++;
                            }

                            strcat(spto, "\\s+");
                            spto += 3;
                        }
                        else
                        {
                            *spto++ = *sp;
                        }
                    }
                    else
                    {
                        *spto++ = *sp;
                    }
                }

                strcpy(final, work);
            }

            if (opt == cf_ignore_leading)
            {
                if (strncmp(final, "\\s*", 3) != 0)
                {
                    for (sp = final; isspace(*sp); sp++)
                    {
                    }
                    strcpy(work, sp);
                    snprintf(final, CF_BUFSIZE, "\\s*%s", work);
                }
            }

            if (opt == cf_ignore_trailing)
            {
                if (strncmp(final + strlen(final) - 4, "\\s*", 3) != 0)
                {
                    strcpy(work, final);
                    snprintf(final, CF_BUFSIZE, "%s\\s*", work);
                }
            }

            ok = ok || FullTextMatch(final, haystack);
        }

        if (!ok)                // All lines in region need to match to avoid insertions
        {
            break;
        }
    }

    DeleteItemList(list);
    return ok;
}

/*********************************************************************/

int MatchRlistItem(Rlist *listofregex, const char *teststring)
   /* Checks whether item matches a list of wildcards */
{
    Rlist *rp;

    for (rp = listofregex; rp != NULL; rp = rp->next)
    {
        /* Avoid using regex if possible, due to memory leak */

        if (strcmp(teststring, rp->item) == 0)
        {
            return (true);
        }

        /* Make it commutative */

        if (FullTextMatch(rp->item, teststring))
        {
            CfDebug("MatchRlistItem(%s > %s)\n", (char *) rp->item, teststring);
            return true;
        }
    }

    return false;
}

/*********************************************************************/
/* Enumerated languages - fuzzy match model                          */
/*********************************************************************/

int FuzzyMatchParse(char *s)
{
    char *sp;
    short isCIDR = false, isrange = false, isv6 = false, isv4 = false, isADDR = false;
    char address[CF_ADDRSIZE];
    int mask, count = 0;

    CfDebug("Check ParsingIPRange(%s)\n", s);

    for (sp = s; *sp != '\0'; sp++)     /* Is this an address or hostname */
    {
        if (!isxdigit((int) *sp))
        {
            isADDR = false;
            break;
        }

        if (*sp == ':')         /* Catches any ipv6 address */
        {
            isADDR = true;
            break;
        }

        if (isdigit((int) *sp)) /* catch non-ipv4 address - no more than 3 digits */
        {
            count++;
            if (count > 3)
            {
                isADDR = false;
                break;
            }
        }
        else
        {
            count = 0;
        }
    }

    if (!isADDR)
    {
        return true;
    }

    if (strstr(s, "/") != 0)
    {
        isCIDR = true;
    }

    if (strstr(s, "-") != 0)
    {
        isrange = true;
    }

    if (strstr(s, ".") != 0)
    {
        isv4 = true;
    }

    if (strstr(s, ":") != 0)
    {
        isv6 = true;
    }

    if (isv4 && isv6)
    {
        CfOut(cf_error, "", "Mixture of IPv6 and IPv4 addresses");
        return false;
    }

    if (isCIDR && isrange)
    {
        CfOut(cf_error, "", "Cannot mix CIDR notation with xx-yy range notation");
        return false;
    }

    if (isv4 && isCIDR)
    {
        if (strlen(s) > 4 + 3 * 4 + 1 + 2)      /* xxx.yyy.zzz.mmm/cc */
        {
            CfOut(cf_error, "", "IPv4 address looks too long");
            return false;
        }

        address[0] = '\0';
        mask = 0;
        sscanf(s, "%16[^/]/%d", address, &mask);

        if (mask < 8)
        {
            CfOut(cf_error, "", "Mask value %d in %s is less than 8", mask, s);
            return false;
        }

        if (mask > 30)
        {
            CfOut(cf_error, "", "Mask value %d in %s is silly (> 30)", mask, s);
            return false;
        }
    }

    if (isv4 && isrange)
    {
        long i, from = -1, to = -1;
        char *sp1, buffer1[CF_MAX_IP_LEN];

        sp1 = s;

        for (i = 0; i < 4; i++)
        {
            buffer1[0] = '\0';
            sscanf(sp1, "%[^.]", buffer1);
            sp1 += strlen(buffer1) + 1;

            if (strstr(buffer1, "-"))
            {
                sscanf(buffer1, "%ld-%ld", &from, &to);

                if (from < 0 || to < 0)
                {
                    CfOut(cf_error, "", "Error in IP range - looks like address, or bad hostname");
                    return false;
                }

                if (to < from)
                {
                    CfOut(cf_error, "", "Bad IP range");
                    return false;
                }

            }
        }
    }

    if (isv6 && isCIDR)
    {
        char address[CF_ADDRSIZE];
        int mask;

        if (strlen(s) < 20)
        {
            CfOut(cf_error, "", "IPv6 address looks too short");
            return false;
        }

        if (strlen(s) > 42)
        {
            CfOut(cf_error, "", "IPv6 address looks too long");
            return false;
        }

        address[0] = '\0';
        mask = 0;
        sscanf(s, "%40[^/]/%d", address, &mask);

        if (mask % 8 != 0)
        {
            CfOut(cf_error, "", "Cannot handle ipv6 masks which are not 8 bit multiples (fix me)");
            return false;
        }

        if (mask > 15)
        {
            CfOut(cf_error, "", "IPv6 CIDR mask is too large");
            return false;
        }
    }

    return true;
}

/*********************************************************************/

void EscapeSpecialChars(char *str, char *strEsc, int strEscSz, char *noEsc)
/* Escapes non-alphanumeric chars, except sequence given in noEsc */
{
    char *sp;
    int strEscPos = 0;

    if (noEsc == NULL)
    {
        noEsc = "";
    }

    memset(strEsc, 0, strEscSz);

    for (sp = str; (*sp != '\0') && (strEscPos < strEscSz - 2); sp++)
    {
        if (strncmp(sp, noEsc, strlen(noEsc)) == 0)
        {
            if (strEscSz <= strEscPos + strlen(noEsc))
            {
                break;
            }

            strcat(strEsc, noEsc);
            strEscPos += strlen(noEsc);
            sp += strlen(noEsc);
        }

        if (*sp != '\0' && !isalnum(*sp))
        {
            strEsc[strEscPos++] = '\\';
        }

        strEsc[strEscPos++] = *sp;
    }
}
    
/*********************************************************************/

void EscapeRegexChars(char *str, char *strEsc, int strEscSz)

{
    char *sp;
    int strEscPos = 0;

    memset(strEsc, 0, strEscSz);

    for (sp = str; (*sp != '\0') && (strEscPos < strEscSz - 2); sp++)
    {
        switch (*sp)
        {
        case '.':
        case '*':
            strEsc[strEscPos++] = '\\';
            break;
        default:
            break;                
        }

        strEsc[strEscPos++] = *sp;
    }
}
    
/*********************************************************************/

char *EscapeChar(char *str, int strSz, char esc)
/* Escapes characters esc in the string str of size strSz  */
{
    char strDup[CF_BUFSIZE];
    int strPos, strDupPos;

    if (sizeof(strDup) < strSz)
    {
        FatalError("Too large string passed to EscapeCharInplace()\n");
    }

    snprintf(strDup, sizeof(strDup), "%s", str);
    memset(str, 0, strSz);

    for (strPos = 0, strDupPos = 0; strPos < strSz - 2; strPos++, strDupPos++)
    {
        if (strDup[strDupPos] == esc)
        {
            str[strPos] = '\\';
            strPos++;
        }

        str[strPos] = strDup[strDupPos];
    }

    return str;
}

/*********************************************************************/

void AnchorRegex(const char *regex, char *out, int outSz)
{
    if (NULL_OR_EMPTY(regex))
    {
        memset(out, 0, outSz);
    }
    else
    {
        snprintf(out, outSz, "^(%s)$", regex);
    }
}

/* EOF */
