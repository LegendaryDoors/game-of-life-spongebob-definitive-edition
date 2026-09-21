# Special card codes

Three codes unlock bonus cards. Enter them at Main Menu > Profile > Bonus Cards > Enter Code.

| Code | Unlocks | Category | Value |
|---|---|---|---|
| `THINKHAPPY` | Jellyfish Hunter (College) | Job | $700 |
| `BESTDAY` | Patrick's Rock | Home | $400 |
| `LUCKYDAY` | Gary | Pet | $500 |

Type them without spaces. Matching is case-insensitive, so `thinkhappy` works too.

## Why there are six card slots but only three codes

Six special cards exist. Only three have codes; the other three are earned by playing. Confirmed by enumerating all 62 `CKDataArray`s in the loaded game:

- `ar_codesUnlock` has 3 rows and is the only array with a `code` column. There is no fourth code anywhere.
- `ar_unlockRules` has 6 rows, one per special card, each unlocking when a player stat passes a threshold.

The three coded cards match three of those rules exactly, so **the codes are shortcuts to cards you could otherwise earn**.

| Card | Category | Value | Code | Earned by |
|---|---|---|---|---|
| Jellyfish Hunter (College) | Job | $700 | `THINKHAPPY` | stat col#9 >= 10000 |
| Patrick's Rock | Home | $400 | `BESTDAY` | stat col#5 >= 20 |
| Gary | Pet | $500 | `LUCKYDAY` | stat col#18 >= 1000 |
| Trailer | Home | $300 | none | stat col#6 >= 100 |
| Artist | Job | $600 | none | stat col#14 >= 25000 |
| Bubble Buddy | Pet | $300 | none | stat col#15 >= 10 |

So the six are two jobs, two homes and two pets, and in each pair one has a code.

## How they were found

The code strings are not in the Virtools schematic's name table; they live in a `CKDataArray` binary chunk. The VSL scripts `checkUnlocksByCodes` and `checkCodeUnlock` run a "Contain String" row search over an array named `CodesArray`, and each matching row yields a `cardID` and `cardType` identifying the card to unlock.

They were read out of the running game with frida and the CK2.dll API: `GetCKContext`, then `CKContext::GetObjectByName("CodesArray")`, `CKParameter::GetValueObject` and `CKDataArray::GetElementStringValue`. Card names come from the `ar_DynamicText` table, where `job_700,2` is "Jellyfish Hunter", `home_400,2` is "Patrick's Rock" and `pet_500,1` is "Gary".
