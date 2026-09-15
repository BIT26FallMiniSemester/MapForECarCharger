import test from 'node:test'
import assert from 'node:assert/strict'
import { pages, pageFromHash } from '../src/api/pages.mjs'

test('all seven pages have stable deep links', () => {
  assert.equal(new Set(pages.map(p => p.id)).size, 7)
  for (const page of pages) assert.equal(pageFromHash(`#/${page.id}`), page)
})
test('empty and unknown hashes fall back to overview', () => {
  for (const hash of ['', '#/', '#/unknown']) assert.equal(pageFromHash(hash).id, 'overview')
})
