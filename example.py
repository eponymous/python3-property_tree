# Download TV show info from TVmaze and store it in a json file
#
# Probably shouldn't actually do this since "tvmaze.json" is something like
# 47 Megabytes and it'll hammer their servers
# ...but, on the other hand, it makes for a nice test file

import requests
import property_tree as ptree


url     = 'http://api.tvmaze.com/shows'
headers = {'Accept': "application/json", 'User-Agent' : "Mozilla/5.0"}
params  = {'page' : 0}
updated = False


try:
    # open the json file
    tree = ptree.json.load('tvmaze.json')
except property_tree.JSONParserError:
    # ...or create a new Tree for the shows
    tree = ptree.Tree()

    # add a 'shows' sub-tree
    tree.shows = ptree.Tree()


if not tree.shows.empty():  
    params['page'] = int(tree.shows[-1].id) // 250

while True:
    with requests.get(url, headers=headers, params=params) as response:
        if response.status_code == 200:
            # load the server response string into a Tree
            feed = ptree.json.loads(response.text)
        else:
            print(f"stopped at page {params['page']}")
            break

    for key, value in feed:
        if value.id not in tree.shows:
            tree.shows.append(value.id.value, value)
            print(value.name)
            updated = True

    print(f"downloaded page {params['page']}")
    params['page'] += 1

if updated is True:
    # sort using a python function -- builtin sort function
    # sorts by string value which isn't what we want here.
    # Actually, this probably isn't even needed since the shows
    # should already be in proper order but anyhoo...
    tree.shows.sort(lambda lhs, rhs: int(lhs[0]) < int(rhs[0]))

    # save the file
    ptree.json.dump('tvmaze.json', tree, pretty_print=False)


###############################################################################
#                     Query (for stuff) examples                              #
###############################################################################
'''
import property_tree as ptree
tree = ptree.json.load('tvmaze.json')

# find all the pandas with lambda
for k, v in tree.shows.search(lambda k, v: "Panda" in v.name):
    print(v.name)


# more complex query with a filter function
def filter_func(k, v):
    return "2018" in v.premiered and \
           "Saturday" in v.schedule.days.values() and \
           v.language == "English" and \
           v.schedule.time == "22:00"

for k, v in tree.shows.search(filter_func):
    print(v.name)


# find all running BBC One shows where __getattribute__ might throw using list comprehension
for show in [v for k, v in tree.shows if v.get('network.id', 0) == 12 and v.status == "Running"]:
    print(show.name)
'''
