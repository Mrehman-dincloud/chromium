#!/usr/bin/python
# Copyright (c) 2010 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os

import pyauto_functional  # Must be imported before pyauto
import pyauto


class HistoryTest(pyauto.PyUITest):
  """TestCase for History."""

  def testBasic(self):
    url = self.GetFileURLForPath(os.path.join(self.DataDir(), 'title2.html'))
    title = 'Title Of Awesomeness'
    self.NavigateToURL(url)

    history = self.GetHistoryInfo().History()
    self.assertEqual(1, len(history))
    self.assertEqual(title, history[0]['title'])
    self.assertEqual(url, history[0]['url'])

  def Debug(self):
    """Test method for experimentation.

    This method will not run automatically.
    """
    while True:
      raw_input('Interact with the browser and hit <enter> to dump history.. ')
      print '*' * 20
      history = self.GetHistoryInfo().History()
      import pprint
      pp = pprint.PrettyPrinter(indent=2)
      pp.pprint(history)

  def testHistoryPersists(self):
    """Verify that history persists after session restart."""
    assert not self.GetHistoryInfo().History(), 'Expecting clean history.'
    url = self.GetFileURLForPath(os.path.join(self.DataDir(), 'title2.html'))
    title = 'Title Of Awesomeness'
    self.NavigateToURL(url)
    history = self.GetHistoryInfo().History()
    self.assertEqual(1, len(history))
    self.assertEqual(title, history[0]['title'])
    self.assertEqual(url, history[0]['url'])
    self.RestartBrowser(clear_profile=False)
    # Verify that history persists.
    history = self.GetHistoryInfo().History()
    self.assertEqual(1, len(history))
    self.assertEqual(title, history[0]['title'])
    self.assertEqual(url, history[0]['url'])

  def testInvalidURLNoHistory(self):
    """Invalid URLs should not go in history."""
    assert not self.GetHistoryInfo().History(), 'Expecting clean history.'
    urls = [ self.GetFileURLForPath('some_non-existing_path'),
             'http://a.nonexisting.domain.blah',
             'ftp://this.should.not.exist/file',
           ]
    for url in urls:
       self.NavigateToURL(url)
    self.assertEqual(0, len(self.GetHistoryInfo().History()))

  def testNewTabNoHistory(self):
    """New tab page - chrome://newtab/ should not show up in history."""
    assert not self.GetHistoryInfo().History(), 'Expecting clean history.'
    self.AppendTab(pyauto.GURL('chrome://newtab/'))
    self.assertEqual(0, len(self.GetHistoryInfo().History()))

  def testIncognitoNoHistory(self):
    """Incognito browsing should not show up in history."""
    assert not self.GetHistoryInfo().History(), 'Expecting clean history.'
    url = self.GetFileURLForPath(os.path.join(self.DataDir(), 'title2.html'))
    self.RunCommand(pyauto.IDC_NEW_INCOGNITO_WINDOW)
    self.NavigateToURL(url, 1, 0)
    self.assertEqual(0, len(self.GetHistoryInfo().History()))

  def testStarredBookmarkInHistory(self):
    """Verify "starred" URLs in history."""
    url = self.GetFileURLForPath(os.path.join(self.DataDir(), 'title2.html'))
    title = 'Title Of Awesomeness'
    self.NavigateToURL(url)

    # Should not be starred in history yet.
    history = self.GetHistoryInfo().History()
    self.assertEqual(1, len(history))
    self.assertFalse(history[0]['starred'])

    # Bookmark the URL.
    bookmarks = self.GetBookmarkModel()
    bar_id = bookmarks.BookmarkBar()['id']
    self.AddBookmarkURL(bar_id, 0, title, url)

    # Should be starred now.
    history = self.GetHistoryInfo().History()
    self.assertEqual(1, len(history))
    self.assertTrue(history[0]['starred'])

    # Remove bookmark.
    bookmarks = self.GetBookmarkModel()
    node = bookmarks.FindByTitle(title)
    self.assertTrue(node)
    id = node[0]['id']
    self.RemoveBookmark(id)

    # Should not be starred anymore.
    history = self.GetHistoryInfo().History()
    self.assertEqual(1, len(history))
    self.assertFalse(history[0]['starred'])

  def testNavigateMultiTimes(self):
    """Multiple navigations to the same url should have a single history."""
    assert not self.GetHistoryInfo().History(), 'Expecting clean history.'
    url = self.GetFileURLForPath(os.path.join(self.DataDir(), 'title2.html'))
    for i in range(5):
      self.NavigateToURL(url)
    self.assertEqual(1, len(self.GetHistoryInfo().History()))

  def testMultiTabsWindowsHistory(self):
    """Verify history with multiple windows and tabs."""
    assert not self.GetHistoryInfo().History(), 'Expecting clean history.'
    urls = []
    for name in ['title2.html', 'title1.html', 'title3.html', 'simple.html']:
       urls.append(self.GetFileURLForPath(os.path.join(self.DataDir(), name)))
    num_urls = len(urls)
    assert num_urls == 4, 'Need 4 urls'

    self.NavigateToURL(urls[0], 0, 0)        # window 0, tab 0
    self.OpenNewBrowserWindow(True)
    self.AppendTab(pyauto.GURL(urls[1]), 0)  # window 0, tab 1
    self.AppendTab(pyauto.GURL(urls[2]), 1)  # window 1
    self.AppendTab(pyauto.GURL(urls[3]), 1)  # window 1

    history = self.GetHistoryInfo().History()
    self.assertEqual(num_urls, len(history))
    # The history should be ordered most recent first.
    for i in range(num_urls):
      self.assertEqual(urls[-1 - i], history[i]['url'])


if __name__ == '__main__':
  pyauto_functional.Main()
